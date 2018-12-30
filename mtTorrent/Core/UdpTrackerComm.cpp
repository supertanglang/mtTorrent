#include "UdpTrackerComm.h"
#include "utils/PacketHelper.h"
#include "utils/Network.h"
#include "Configuration.h"
#include "Logging.h"
#include "Torrent.h"

#define UDP_TRACKER_LOG(x) WRITE_LOG(LogTypeUdpTracker, info.hostname << " " << x)

using namespace mtt;

mtt::UdpTrackerComm::UdpTrackerComm()
{
}

void UdpTrackerComm::init(std::string host, std::string port, TorrentPtr t)
{
	info.hostname = host;
	torrent = t;

	udp = UdpAsyncComm::Get();
	comm = udp->create(host, port);
}

DataBuffer UdpTrackerComm::createConnectRequest()
{
	auto transaction = (uint32_t)rand();
	uint64_t connectId = 0x41727101980;

	lastMessage = { Connnect, transaction };

	PacketBuilder packet;
	packet.add64(connectId);
	packet.add32(Connnect);
	packet.add32(transaction);

	return packet.getBuffer();
}

void mtt::UdpTrackerComm::fail()
{
	UDP_TRACKER_LOG("fail");

	if (info.state < Connected)
		info.state = Initialized;
	else if (info.state < Announced)
		info.state = Connected;
	else
		info.state = Announced;

	if (onFail)
		onFail();
}

bool mtt::UdpTrackerComm::onConnectUdpResponse(UdpRequest comm, DataBuffer* data)
{
	if (!data)
	{
		fail();

		return false;
	}

	auto response = getConnectResponse(*data);

	if (validResponse(response))
	{
		info.state = Connected;
		connectionId = response.connectionId;

		announce();

		return true;
	}
	else if (response.transaction == lastMessage.transaction)
	{
		fail();

		return true;
	}

	return false;
}

DataBuffer UdpTrackerComm::createAnnounceRequest()
{
	auto transaction = (uint32_t)rand();

	lastMessage = { Announce, transaction };

	PacketBuilder packet;
	packet.add64(connectionId);
	packet.add32(Announce);
	packet.add32(transaction);

	packet.add(torrent->infoFile.info.hash, 20);
	packet.add(mtt::config::internal_.hashId, 20);

	packet.add64(0);
	packet.add64(0);
	packet.add64(0);

	packet.add32(Started);
	packet.add32(0);

	packet.add32(mtt::config::internal_.trackerKey);
	packet.add32(mtt::config::internal_.maxPeersPerTrackerRequest);
	packet.add32(mtt::config::external.tcpPort);
	packet.add16(0);

	return packet.getBuffer();
}

bool mtt::UdpTrackerComm::onAnnounceUdpResponse(UdpRequest comm, DataBuffer* data)
{
	if (!data)
	{
		fail();

		return false;
	}

	auto announceMsg = getAnnounceResponse(*data);

	if (validResponse(announceMsg.udp))
	{
		UDP_TRACKER_LOG("received peers:" << announceMsg.peers.size() << ", p: " << announceMsg.seedCount << ", l: " << announceMsg.leechCount);
		info.state = Announced;
		info.leechers = announceMsg.leechCount;
		info.seeds = announceMsg.seedCount;
		info.peers = (uint32_t)announceMsg.peers.size();
		info.announceInterval = announceMsg.interval;
		info.lastAnnounce = (uint32_t)::time(0);

		if (onAnnounceResult)
			onAnnounceResult(announceMsg);

		return true;
	}
	else if (announceMsg.udp.transaction == lastMessage.transaction)
	{
		fail();

		return true;
	}

	return false;
}

void mtt::UdpTrackerComm::connect()
{
	UDP_TRACKER_LOG("connecting");
	info.state = Connecting;

	udp->sendMessage(createConnectRequest(), comm, std::bind(&UdpTrackerComm::onConnectUdpResponse, this, std::placeholders::_1, std::placeholders::_2));
}

bool mtt::UdpTrackerComm::validResponse(TrackerMessage& resp)
{
	return resp.action == lastMessage.action && resp.transaction == lastMessage.transaction;
}

void mtt::UdpTrackerComm::announce()
{
	if (info.state < Connected)
		connect();
	else
	{
		UDP_TRACKER_LOG("announcing");

		if (info.state == Announced)
			info.state = Reannouncing;
		else
			info.state = Announcing;

		udp->sendMessage(createAnnounceRequest(), comm, std::bind(&UdpTrackerComm::onAnnounceUdpResponse, this, std::placeholders::_1, std::placeholders::_2));
	}
}

UdpTrackerComm::ConnectResponse UdpTrackerComm::getConnectResponse(DataBuffer& buffer)
{
	ConnectResponse out;

	if (buffer.size() >= sizeof ConnectResponse)
	{
		PacketReader packet(buffer);

		out.action = packet.pop32();
		out.transaction = packet.pop32();
		out.connectionId = packet.pop64();
	}

	return out;
}

UdpTrackerComm::UdpAnnounceResponse UdpTrackerComm::getAnnounceResponse(DataBuffer& buffer)
{
	PacketReader packet(buffer);

	UdpAnnounceResponse resp;

	resp.udp.action = packet.pop32();
	resp.udp.transaction = packet.pop32();

	if (resp.udp.action == Error && buffer.size() > 8)
	{
		UDP_TRACKER_LOG("announce msg: " << (const char*)buffer.data() + 8);
	}

	if (buffer.size() < 26)
		return resp;

	resp.interval = packet.pop32();
	resp.leechCount = packet.pop32();
	resp.seedCount = packet.pop32();

	size_t count = static_cast<size_t>(packet.getRemainingSize() / 6.0f);

	for (size_t i = 0; i < count; i++)
	{
		uint32_t ip = *reinterpret_cast<const uint32_t*>(packet.popRaw(sizeof(uint32_t)));
		resp.peers.push_back(Addr(ip, packet.pop16()));
	}

	return resp;
}
