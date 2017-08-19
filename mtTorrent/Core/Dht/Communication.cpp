#include "Dht/Communication.h"
#include "Configuration.h"

mtt::dht::Communication::Communication() : udpMgr(service.io, mtt::config::external.udpPort)
{
	service.start(3);
	udpMgr.listen(std::bind(&Communication::onNewUdpPacket, this, std::placeholders::_1, std::placeholders::_2));
}

mtt::dht::Communication::~Communication()
{
	peersQueries.clear();
	service.stop();
}

void mtt::dht::Communication::removeListener(ResultsListener* listener)
{
	std::lock_guard<std::mutex> guard(peersQueriesMutex);

	for (auto it = peersQueries.begin(); it != peersQueries.end();)
	{
		if (it->listener == listener)
			it = peersQueries.erase(it);
		else
			it++;
	}
}

bool operator== (std::shared_ptr<mtt::dht::Query::DhtQuery> query, uint8_t* hash)
{
	return memcmp(query->targetId.data, hash, 20) == 0;
}

void mtt::dht::Communication::findPeers(uint8_t* hash, ResultsListener* listener)
{
	{
		std::lock_guard<std::mutex> guard(peersQueriesMutex);

		for (auto it = peersQueries.begin(); it != peersQueries.end(); it++)
			if (it->q == hash)
				return;
	}

	QueryInfo info;
	info.q = std::make_shared<Query::FindPeers>();
	info.listener = listener;
	peersQueries.push_back(info);

	info.q->start(hash, &table, this);
}

void mtt::dht::Communication::stopFindingPeers(uint8_t* hash)
{
	std::lock_guard<std::mutex> guard(peersQueriesMutex);

	for (auto it = peersQueries.begin(); it != peersQueries.end(); it++)
		if (it->q == hash)
		{
			peersQueries.erase(it);
			break;
		}
}

void mtt::dht::Communication::findNode(uint8_t* hash)
{
	auto q = fnQ = std::make_shared<Query::FindNode>();
	q->start(hash, &table, this);
}

void mtt::dht::Communication::pingNode(Addr& addr, uint8_t* hash)
{
	auto q = std::make_shared<Query::PingNodes>();

	auto bucketId = hash ? table.getBucketId(hash) : 0;
	q->start(addr, bucketId, &table, this);
}

bool mtt::dht::Communication::onNewUdpPacket(UdpRequest, DataBuffer*)
{
	return false;
}

uint32_t mtt::dht::Communication::onFoundPeers(uint8_t* hash, std::vector<Addr>& values)
{
	for (auto info : peersQueries)
	{
		if (info.listener && info.q == hash)
		{
			uint32_t c = info.listener->onFoundPeers(hash, values);

			if (c)
				return c;
		}
	}

	return (uint32_t)values.size();
}

void mtt::dht::Communication::findingPeersFinished(uint8_t* hash, uint32_t count)
{
	ResultsListener* listener = nullptr;

	{
		std::lock_guard<std::mutex> guard(peersQueriesMutex);

		for (auto it = peersQueries.begin(); it != peersQueries.end(); it++)
			if (it->q == hash)
			{
				listener = it->listener;
				peersQueries.erase(it);
				break;
			}
	}

	if(listener)
		listener->findingPeersFinished(hash, count);
}

UdpRequest mtt::dht::Communication::sendMessage(Addr& addr, DataBuffer& data, UdpResponseCallback response)
{
	return udpMgr.sendMessage(data, addr, response);
}

void mtt::dht::Communication::loadDefaultRoots()
{
	auto resolveFunc = [this]
	(const boost::system::error_code& error, udp::resolver::iterator iterator, std::shared_ptr<udp::resolver> resolver)
	{
		if (!error)
		{
			udp::resolver::iterator end;

			while (iterator != end)
			{
				Addr a;
				a.set(iterator->endpoint().address(), iterator->endpoint().port());
				pingNode(a, nullptr);

				iterator++;
			}
		}
	};

	for (auto& r : mtt::config::internal.defaultRootHosts)
	{
		udp::resolver::query query(r.first, r.second);
		auto resolver = std::make_shared<udp::resolver>(service.io);
		resolver->async_resolve(query, std::bind(resolveFunc, std::placeholders::_1, std::placeholders::_2, resolver));
	}
}

void mtt::dht::Communication::refreshTable()
{
	auto inactive = table.getInactiveNodes();

	auto q = std::make_shared<Query::PingNodes>();
	q->start(inactive, &table, this);
}

std::string mtt::dht::Communication::save()
{
	return table.save();
}

void mtt::dht::Communication::load(std::string& settings)
{
	uint32_t nodesCount = table.load(settings);
	refreshTable();

	loadDefaultRoots();

	if(nodesCount < 50)
		findNode(mtt::config::internal.hashId);
	
	refreshTimer = ScheduledTimer::create(service.io, std::bind(&Communication::refreshTable, this));
	refreshTimer->schedule(5 * 60 + 5);
}

#ifdef true
char fromHexa(char h)
{
	if (h <= '9')
		h = h - '0';
	else
		h = h - 'a' + 10;

	return h;
}

std::vector<Addr> Communication::get()
{
	/*NodeId testdata;
	memset(testdata.data, 0, 20);
	testdata.data[2] = 8;
	auto l = testdata.length();*/

	const char* dhtRoot = "dht.transmissionbt.com";
	const char* dhtRootPort = "6881";

	bool ipv6 = true;

	boost::asio::io_service io_service;
	udp::resolver resolver(io_service);

	udp::socket sock_v6(io_service);
	sock_v6.open(udp::v6());

	udp::socket sock_v4(io_service);
	sock_v4.open(udp::v4());

	std::string myId(20,0);
	for (int i = 0; i < 20; i++)
	{
		myId[i] = 5 + i * 5;
	}

	std::string targetIdBase32 = "T323KFN5XLZAZZO2NDNCYX7OBMQTUV6U"; // "ZEF3LK3MCLY5HQGTIUVAJBFMDNQW6U3J";
	auto targetId = base32decode(targetIdBase32);

	/*auto hexaStr = "c90bb8324012a7d515901f4b3edaf02f02a5fae9";
	for (size_t i = 0; i < 20; i++)
	{
		char f = fromHexa(hexaStr[i * 2]);
		char s = fromHexa(hexaStr[i * 2 + 1]);

		targetId[i] = (f << 4) + s;
	}*/

	const char* clientId = "mt02";
	uint16_t transactionId = 54535;

	PacketBuilder packet(104);
	packet.add("d1:ad2:id20:",12);
	packet.add(myId.data(), 20);
	packet.add("9:info_hash20:",14);
	packet.add(targetId.data(), 20);
	packet.add("e1:q9:get_peers1:v4:",20);
	packet.add(clientId,4);
	packet.add("1:t2:",5);
	packet.add(reinterpret_cast<char*>(&transactionId),2);
	packet.add("1:y1:qe",7);

	PacketBuilder ipv4packet = packet;
	ipv4packet.addAfter(targetId.data(), "4:wantl2:n42:n6e", 16);

	std::vector<NodeInfo> receivedNodes;

	try
	{	
		auto message = sendUdpRequest(ipv6 ? sock_v6 : sock_v4, resolver, packet.getBuffer(), dhtRoot, dhtRootPort, 5000, ipv6);
		auto resp = parseGetPeersResponse(message);
		std::vector<NodeInfo> nextNodes = resp.nodes;
		
	/*	std::vector<NodeInfo> nextNodes;
		NodeInfo info;
		info.addr.str = "127.0.0.1";
		info.addr.port = 56572;
		nextNodes.push_back(info);*/

		std::vector<NodeInfo> usedNodes;
		std::vector<Addr> values;
		NodeId minDistance = nextNodes.front().id;
		NodeId targetIdNode(targetId.data());

		bool firstWave = false;

		while (!nextNodes.empty() && values.empty())
		{
			auto currentNodes = nextNodes;
			usedNodes.insert(usedNodes.end(), currentNodes.begin(), currentNodes.end());

			nextNodes.clear();

#ifdef ASYNC_DHT
			std::vector<std::future<GetPeersResponse>> f;
#endif

			for(size_t i = 0; i < currentNodes.size(); i++)
			{
				auto& node = currentNodes[i];

#ifdef ASYNC_DHT
				if (f.size() >= 5)
				{
					for (auto& r : f)
					{
						auto resp = r.get();
						mergeClosestNodes(nextNodes, resp.nodes, usedNodes, 64, minDistance, targetIdNode);
					}

					f.clear();
				}

				f.push_back(std::async([&]() 
				{
					GetPeersResponse resp;

					try
					{
						auto message = sendUdpRequest(node.addr.ipv6 ? sock_v6 : sock_v4, firstWave ? packet.getBuffer() : ipv4packet.getBuffer(), node.addr.str.data(), node.addr.port, 3000);
						resp = parseGetPeersResponse(message);
					}
					catch (const std::exception&e)
					{
						DHT_LOG("DHT exception: " << e.what() << "\n");
					}

					return resp;
				}));
#else
				try
				{
					auto message = sendUdpRequest(node.addr.ipv6 ? sock_v6 : sock_v4, firstWave ? packet.getBuffer() : ipv4packet.getBuffer(), node.addr.addrBytes, node.addr.port, 3000);
					auto resp = parseGetPeersResponse(message);
					mergeClosestNodes(nextNodes, resp.nodes, usedNodes, 16, minDistance, targetIdNode);

					values.assign(resp.values.begin(), resp.values.end());
				}
				catch (const std::exception&e)
				{
					DHT_LOG("DHT exception: " << e.what() << "\n");
				}
#endif
			}

#ifdef ASYNC_DHT
			if (!f.empty())
			{
				for (auto& r : f)
				{
					auto resp = r.get();
					mergeClosestNodes(nextNodes, resp.nodes, usedNodes, 32, minDistance, targetIdNode);
				}

				f.clear();
			}
#endif

			if (!nextNodes.empty())
			{
				minDistance = getShortestDistance(nextNodes, targetIdNode);
				DHT_LOG("DHT distance: " << (int)minDistance.length() << " next nodes count: " << nextNodes.size() << "\n");
			}

			firstWave = false;
		}

		if (!values.empty())
		{
			DHT_LOG("DHT returned values count: " << values.size() << "\n");

			/*for (auto& addr : values)
			{
				try
				{
					auto message = sendUdpRequest(addr.isIpv6() ? sock_v6 : sock_v4, firstWave ? packet.getBuffer() : ipv4packet.getBuffer(), addr.addrBytes.data(), addr.port, 3000);
					auto resp = parseGetPeersResponse(message);
				}
				catch (const std::exception&e)
				{
					DHT_LOG("DHT exception: " << e.what() << "\n");
				}
			}*/
		}

		return values;
	}
	catch (const std::exception&e)
	{
		DHT_LOG("DHT exception: " << e.what() << "\n");
	}

	return{};
}


#endif