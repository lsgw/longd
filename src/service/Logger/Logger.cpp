#include "rapidjson/document.h"
#include "Actor.h"
#include <string>
using namespace rapidjson;

class Logger : public Actor<Logger> {
public:
	void init(ContextPtr ctx, MessagePtr& message) override
	{
		assert(message->type == MSG_TYPE_JSON);	
		fp_ = NULL;
		startOfPeriod_ = 0;
		lastRoll_ = 0;
		count_ = 0;
		writtenBytes_ = 0;
		logLevel_ = 0;

		std::string pathname;

		Document document;
		document.Parse(static_cast<char*>(message->data), message->size);
		assert(document.IsObject());
		if (document.HasMember("level") && document["level"].IsString()) {
			logLevel_ = switchLogLevelToNum(document["level"].GetString());
		} else {
			logLevel_ = 2;
		}
		if (document.HasMember("path") && document["path"].IsString()) {
			pathname  = document["path"].GetString();
		}

		if (document.HasMember("RollCheckInterval") && document["RollCheckInterval"].IsInt()) {
			kRollCheckInterval_  = document["RollCheckInterval"].GetInt();
		}
		if (document.HasMember("RollPerSeconds") && document["RollPerSeconds"].IsInt()) {
			kRollPerSeconds_  = document["RollPerSeconds"].GetInt();
		}
		if (document.HasMember("path") && document["path"].IsInt()) {
			kRollSize_  = document["RollSize"].GetInt();
		}



		// fprintf(stderr, "%d : %s\n", logLevel_, pathname.c_str());

		if (pathname.empty()) {
			fp_ = stdout;
		} else {
			basename_ = pathname;
			rollFile();
		}

		if (fp_) {
			fprintf(fp_, "[%010d] INFO Launch Logger\n", ctx->handle());
		} else {
			fprintf(stderr, "[%010d] FATAL Launch Logger error\n", ctx->handle());
			ctx->abort();
		}
	}

	bool receive(ContextPtr ctx, MessagePtr& message) override
	{

		if (message->type == MSG_TYPE_LOG) {
			uint32_t level = *((uint32_t*)message->data);
			if (level < logLevel_) {
				return true;
			}

			std::string msg = switchLogLevelToString(level) + " ";
			msg.append(((char*)message->data)+sizeof(uint32_t), message->size - sizeof(uint32_t));

			fprintf(fp_, "[%010d] %.*s\n", message->source, static_cast<int>(msg.size()), msg.data());
			writtenBytes_ += msg.size();

			if (writtenBytes_ >= kRollSize_) {
				rollFile();
			} else {
				if (count_ > kRollCheckInterval_) {
					time_t now = ::time(NULL);
					time_t thisPeriod_ = now / kRollPerSeconds_ * kRollPerSeconds_;
					if (thisPeriod_ != startOfPeriod_) {
						rollFile();
					}
					count_ = 0;
					fflush(fp_);
				} else {
					count_++;
				}
			}
		} else {
			fprintf(fp_, "[%010d] not log mssage\n", message->source);
		}
		return true;

	}
	void release(ContextPtr ctx, MessagePtr& message) override
	{
		fprintf(fp_, "[%010d] Logger exit\n", ctx->handle());
		fclose(fp_);
	}

	void rollFile()
	{
		writtenBytes_ = 0;
		if (!basename_.empty()) {
			time_t now = 0;
			std::string filename = getLogFileName(basename_, &now);
			time_t start = now / kRollPerSeconds_ * kRollPerSeconds_;

			if (now > lastRoll_) {
				lastRoll_ = now;
				startOfPeriod_ = start;
				if (fp_ != NULL) {
					fclose(fp_);
				}
				fp_ = fopen(filename.c_str(), "ae");
			}
		}
	}

	std::string getLogFileName(const std::string& basename, time_t* now)
	{
		std::string filename;
		filename.reserve(basename.size() + 64);
		filename = basename;


		char timebuf[32];
		struct tm tm;
		*now = time(NULL);
		localtime_r(now, &tm);
		// gmtime_r(now, &tm);
		strftime(timebuf, sizeof timebuf, "-%Y.%m.%d-%H:%M:%S-", &tm);
		filename += timebuf;

		char hostbuf[256] = "unknownhost";
		if (::gethostname(hostbuf, sizeof(hostbuf)) == 0) {
			hostbuf[sizeof(hostbuf)-1] = '\0';
		}
		filename += hostbuf;

		char pidbuf[32];
		snprintf(pidbuf, sizeof(pidbuf), "-%d", ::getpid());
		filename += pidbuf;

		filename += ".log";

		return filename;
	}

	uint32_t switchLogLevelToNum(const std::string& level)
	{
		if (level == "TRACE") {
			return 0;
		} else if (level == "DEBUG") {
			return 1;
		} else if (level == "INFO") {
			return 2;
		} else if (level == "WARN") {
			return 3;
		} else if (level == "ERROR") {
			return 4;
		} else if (level == "FATAL") {
			return 5;
		} else {
			return 99;
		}
	}
	std::string switchLogLevelToString(uint32_t level)
	{
		if (level == 0) {
			return "TRACE";
		} else if (level == 1) {
			return "DEBUG";
		} else if (level == 2) {
			return "INFO";
		} else if (level == 3) {
			return "WARN";
		} else if (level == 4) {
			return "ERROR";
		} else if (level == 5) {
			return "FATAL";
		} else {
			return "what happen";
		}
	}

private:
	std::string basename_;
	FILE*       fp_;
	time_t      startOfPeriod_;
	time_t      lastRoll_;
	uint32_t    count_;
	uint64_t    writtenBytes_;

	uint32_t    logLevel_;

	uint32_t    kRollCheckInterval_ = 0;         // 
	uint32_t    kRollPerSeconds_ = 60 * 60 * 24; // one day
	uint64_t    kRollSize_ = 1024 * 1024 * 1024; // 1GB
};


module(Logger)