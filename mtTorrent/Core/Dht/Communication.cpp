#include "Dht/Communication.h"
#include "Configuration.h"
#include <fstream>

mtt::dht::Communication* comm;

mtt::dht::Communication::Communication() : responder(table, *this)
{
	udp = UdpAsyncComm::Get();
	udp->listen(std::bind(&Communication::onUnknownUdpPacket, this, std::placeholders::_1, std::placeholders::_2));
	comm = this;
}

mtt::dht::Communication::~Communication()
{
}

mtt::dht::Communication& mtt::dht::Communication::get()
{
	return *comm;
}

void mtt::dht::Communication::start()
{
	service.start(2);

	load();

	refreshTable();

	loadDefaultRoots();

	findNode(mtt::config::internal_.hashId);

	refreshTimer = ScheduledTimer::create(service.io, std::bind(&Communication::refreshTable, this));
	refreshTimer->schedule(5 * 60 + 5);

	//std::make_shared<Query::PingNodes>()->start(Addr({ 83,26,144,62 }, 44035) , &table, this);
}

void mtt::dht::Communication::stop()
{
	{
		std::lock_guard<std::mutex> guard(peersQueriesMutex);
		peersQueries.clear();
	}

	save();

	if(refreshTimer)
		refreshTimer->disable();
	refreshTimer = nullptr;
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
	info.q = std::make_shared<Query::GetPeers>();
	info.listener = listener;
	{
		std::lock_guard<std::mutex> guard(peersQueriesMutex);
		peersQueries.push_back(info);
	}

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
	auto q = std::make_shared<Query::FindNode>();
	q->start(hash, &table, this);
}

void mtt::dht::Communication::pingNode(Addr& addr)
{
	auto q = std::make_shared<Query::PingNodes>();
	q->start(addr, &table, this);
}

uint32_t mtt::dht::Communication::onFoundPeers(uint8_t* hash, std::vector<Addr>& values)
{
	std::lock_guard<std::mutex> guard(peersQueriesMutex);

	for (auto info : peersQueries)
	{
		if (info.listener && info.q == hash)
		{
			uint32_t c = info.listener->dhtFoundPeers(hash, values);
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
		listener->dhtFindingPeersFinished(hash, count);
}

UdpRequest mtt::dht::Communication::sendMessage(Addr& addr, DataBuffer& data, UdpResponseCallback response)
{
	return udp->sendMessage(data, addr, response);
}

void mtt::dht::Communication::sendMessage(udp::endpoint& endpoint, DataBuffer& data)
{
	udp->sendMessage(data, endpoint);
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
				pingNode(Addr{ iterator->endpoint().address(), iterator->endpoint().port() });

				iterator++;
			}
		}
	};

	for (auto& r : mtt::config::internal_.defaultRootHosts)
	{
		udp::resolver::query query(r.first, r.second);
		auto resolver = std::make_shared<udp::resolver>(service.io);
		resolver->async_resolve(query, std::bind(resolveFunc, std::placeholders::_1, std::placeholders::_2, resolver));
	}
}

void mtt::dht::Communication::refreshTable()
{
	responder.refresh();

	auto inactive = table.getInactiveNodes();

	if (!inactive.empty())
	{
		auto q = std::make_shared<Query::PingNodes>();
		q->start(inactive, &table, this);
	}
}

void mtt::dht::Communication::save()
{
	auto saveFile = table.save();

	{
		std::ofstream out(mtt::config::internal_.programFolderPath + "dht", std::ios_base::binary);
		out << saveFile;
	}
}

void mtt::dht::Communication::load()
{
	std::ifstream inFile(mtt::config::internal_.programFolderPath + "dht", std::ios_base::binary | std::ios_base::in);

	if (inFile)
	{
		auto saveFile = std::string((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
		table.load(saveFile);
	}
}

void mtt::dht::Communication::announceTokenReceived(uint8_t* hash, std::string& token, udp::endpoint& source)
{
	//Query::AnnouncePeer(hash, token, source, this);
}

bool mtt::dht::Communication::onUnknownUdpPacket(udp::endpoint& e, DataBuffer& data)
{
	return responder.handlePacket(e, data);
}