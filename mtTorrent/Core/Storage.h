#pragma once

#include "Interface.h"
#include <mutex>
#include <atomic>

namespace mtt
{
	struct PiecesCheck
	{
		uint32_t piecesCount = 0;
		std::atomic<uint32_t> piecesChecked = 0;
		bool rejected = false;
		std::vector<uint8_t> bitfield;
	};

	class Storage
	{
	public:

		Storage(uint32_t pieceSize);

		void setPath(std::string path);

		void storePiece(DownloadedPiece& piece);
		PieceBlock getPieceBlock(PieceBlockInfo& piece);

		void setSelection(DownloadSelection& files);
		std::vector<uint8_t> checkStoredPieces(std::vector<PieceInfo>& piecesInfo);
		std::shared_ptr<PiecesCheck> checkStoredPiecesAsync(std::vector<PieceInfo>& piecesInfo, boost::asio::io_service& io, std::function<void()> onFinish);
		void flush();

		void saveProgress();
		void loadProgress();	

		DownloadSelection selection;
		uint32_t pieceSize;

		std::vector<PieceBlockInfo> makePieceBlocksInfo(uint32_t index);

	private:

		void checkStoredPieces(PiecesCheck& checkState, const std::vector<PieceInfo>& piecesInfo);

		std::string getFullpath(File& file);
		void createPath(std::string& path);

		void flushAllFiles();
		void flush(File& file);
		void preallocate(File& file);

		std::string path;

		template<typename T, uint32_t max>
		struct CachedData
		{
			std::array<T, max> data;
			uint32_t nextpos = 0;
			uint32_t count = 0;

			void reset()
			{
				nextpos = count = 0;
			}

			T& getNext()
			{
				count = std::min(max, count + 1);
				T& val = data[nextpos];
				nextpos = (nextpos + 1 == max) ? 0 : nextpos + 1;
				return val;
			}
		};

		CachedData<DownloadedPiece, 6> unsavedPieces;
		std::mutex storageMutex;

		struct CachedPiece
		{
			uint32_t index;
			DataBuffer data;
		};
		CachedData<CachedPiece, 16> cachedPieces;
		std::mutex cacheMutex;

		CachedPiece& loadPiece(uint32_t pieceId);
		void loadPiece(File& file, CachedPiece& piece);

	};
}