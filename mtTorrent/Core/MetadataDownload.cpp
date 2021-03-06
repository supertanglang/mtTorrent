#include "MetadataDownload.h"
#include "MetadataReconstruction.h"
#include "Torrent.h"
#include "Configuration.h"

#define BT_UTM_LOG(x) WRITE_LOG(LogTypeBtUtm, x)

mtt::MetadataDownload::MetadataDownload(Peers& p) : peers(p)
{
}

void mtt::MetadataDownload::start(std::function<void(Status, MetadataDownloadState&)> f, boost::asio::io_service& io)
{
	onUpdate = f;
	active = true;

	//peers.trackers.removeTrackers();

	peers.start([this](Status s, mtt::PeerSource source)
	{
		std::lock_guard<std::mutex> guard(commsMutex);
		if (s == Status::Success && active)
		{
			evalComms();
		}
	}
	, this);

	//peers.connect(Addr({ 127,0,0,1 }, 31132));

	std::lock_guard<std::mutex> guard(commsMutex);
	retryTimer = ScheduledTimer::create(io, [this]()
		{
			auto currentTime = (uint32_t) time(0);
			if (active && !state.finished && lastActivityTime + 5 < currentTime)
			{
				std::lock_guard<std::mutex> guard(commsMutex);
				for (auto a : activeComms)
				{
					requestPiece(a);
				}

				if(retryTimer)
					retryTimer->schedule(5);
			}
		}
	);

	retryTimer->schedule(5);
}

void mtt::MetadataDownload::stop()
{
	if (retryTimer)
	{
		retryTimer->disable();
		retryTimer = nullptr;
	}

	addEventLog(nullptr, EventInfo::End, 0);

	if(!state.finished && active)
		onUpdate(Status::I_Stopped, state);

	active = false;
	activeComms.clear();
	peers.stop();
}

std::vector<mtt::MetadataDownload::EventInfo> mtt::MetadataDownload::getEvents()
{
	std::lock_guard<std::mutex> guard(commsMutex);
	return eventLog;
}

uint32_t mtt::MetadataDownload::getEventsCount()
{
	return (uint32_t)eventLog.size();
}

void mtt::MetadataDownload::evalComms()
{
	if (activeComms.size() < mtt::config::external.maxTorrentConnections && !state.finished)
	{
		addEventLog(nullptr, EventInfo::Searching, (uint32_t)activeComms.size());
		peers.connectNext(mtt::config::external.maxTorrentConnections);
		//BT_UTM_LOG("searching for more peers");
	}
}

void mtt::MetadataDownload::removeBackup(PeerCommunication* p)
{
	for (auto it = activeComms.begin(); it != activeComms.end(); it++)
	{
		if (it->get() == p)
		{
			addEventLog(p->info.id, EventInfo::Disconnected, 0);
			activeComms.erase(it);
			break;
		}
	}

	evalComms();
}

void mtt::MetadataDownload::addToBackup(std::shared_ptr<PeerCommunication> peer)
{
	std::lock_guard<std::mutex> guard(commsMutex);

	if (peer->ext.utm.size)
	{
		if (metadata.pieces == 0)
			metadata.init(peer->ext.utm.size);
	
		addEventLog(peer->info.id, EventInfo::Connected, 0);
		activeComms.push_back(peer);
		requestPiece(peer);
	}
}

void mtt::MetadataDownload::handshakeFinished(PeerCommunication* p)
{
	if (!p->info.supportsExtensions())
	{
		BT_UTM_LOG("unwanted");

		peers.disconnect(p);
	}
}

void mtt::MetadataDownload::connectionClosed(PeerCommunication* p, int)
{
	std::lock_guard<std::mutex> guard(commsMutex);

	onUpdate(Status::E_ConnectionClosed, state);

	removeBackup(p);
}

void mtt::MetadataDownload::messageReceived(PeerCommunication* p, PeerMessage& msg)
{
}

void mtt::MetadataDownload::metadataPieceReceived(PeerCommunication* p, ext::UtMetadata::Message& msg)
{
	std::lock_guard<std::mutex> guard(commsMutex);
	addEventLog(p->info.id, EventInfo::Receive, msg.piece);
	//BT_UTM_LOG("received piece idx " << msg.piece);
	metadata.addPiece(msg.metadata, msg.piece);
	state.partsCount = metadata.pieces;
	state.receivedParts++;

	if (metadata.finished() && active)
	{
		state.finished = true;
	}
	else
	{
		auto peer = peers.getPeer(p);

		if (peer)
		{
			requestPiece(peer);
		}
	}

	onUpdate(Status::Success, state);

	if(state.finished)
		stop();
}

void mtt::MetadataDownload::extHandshakeFinished(PeerCommunication* peer)
{
	if (peer->ext.isSupported(ext::MessageType::UtMetadataEx))
		addToBackup(peers.getPeer(peer));
	else
	{
		peers.disconnect(peer);
	}
}

void mtt::MetadataDownload::pexReceived(PeerCommunication*, ext::PeerExchange::Message&)
{
}

void mtt::MetadataDownload::progressUpdated(PeerCommunication*)
{
}

void mtt::MetadataDownload::requestPiece(std::shared_ptr<PeerCommunication> peer)
{
	if (active && !state.finished)
	{
		uint32_t mdPiece = metadata.getMissingPieceIndex();
		peer->ext.requestMetadataPiece(mdPiece);
		memcpy(state.source, peer->info.id, 20);
		//BT_UTM_LOG("requesting piece idx " << mdPiece);
		addEventLog(peer->info.id, EventInfo::Request, mdPiece);
		onUpdate(Status::I_Requesting, state);
		lastActivityTime = (uint32_t)time(0);
	}
}

void mtt::MetadataDownload::addEventLog(uint8_t* id, EventInfo::Action action, uint32_t index)
{
	if (!eventLog.empty() && action == EventInfo::Searching && eventLog.back().action == action && eventLog.back().index == index)
		return;

	EventInfo info;
	info.action = action;
	info.index = index;
	if (id)
		memcpy(info.sourceId, id, 20);

	eventLog.emplace_back(info);
}
