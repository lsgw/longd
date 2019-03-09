#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"  
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Logger/Log.h"
#include "Actor.h"
#include "utils.h"
#include <assert.h>
#include <functional>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <limits>

using namespace std::placeholders;
using namespace rapidjson;

std::string u32_to_string(uint32_t value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%010d", value);
    return std::string(buf);
}

class Launcher : public Actor<Launcher> {
public:
	void init(ContextPtr ctx, MessagePtr& message) override
	{
		assert(ctx->handle() == 1);
		assert(message->type == MSG_TYPE_JSON);
		commands_.insert({"launch",   std::bind(&Launcher::serviceLaunch,   this, _1, _2)});
		commands_.insert({"register", std::bind(&Launcher::serviceRegister, this, _1, _2)});
		commands_.insert({"query",    std::bind(&Launcher::serviceQuery,    this, _1, _2)});
		commands_.insert({"kill",     std::bind(&Launcher::serviceKill,     this, _1, _2)});
		commands_.insert({"exit",     std::bind(&Launcher::serviceExit,     this, _1, _2)});
		commands_.insert({"online",   std::bind(&Launcher::serviceOnline,   this, _1, _2)});
		commands_.insert({"info",     std::bind(&Launcher::serviceInfo,     this, _1, _2)});
		commands_.insert({"portctl",  std::bind(&Launcher::portCtl,         this, _1, _2)});
		commands_.insert({"portkill", std::bind(&Launcher::portKill,        this, _1, _2)});

		Document document;
		document.Parse(static_cast<char*>(message->data), message->size);
		
		assert(document.IsObject());
		assert(document.HasMember("launch"));
		assert(document["launch"].IsArray());

		Value  launchList = document["launch"].GetArray();
		assert(launchList.Size() > 0);

		launchLogger(ctx, launchList[0]);

		for (SizeType i=1; i<launchList.Size(); i++) {
			rapidjson::Value& m = launchList[i];

			assert(m.IsObject());
			assert(m.HasMember("service"));
			assert(m.HasMember("args"));
			assert(m["service"].IsString());
			assert(m["args"].IsArray());
			
			std::string service    = m["service"].GetString();
			rapidjson::Value array = m["args"].GetArray();
			assert(array.Size() > 0);

			StringBuffer buffer;
			Writer<rapidjson::StringBuffer> writer(buffer);
			array.Accept(writer);
			std::string args = buffer.GetString();

			char* data = (char*)malloc(args.size());
			memcpy(data, args.data(), args.size());

			uint32_t handle = ctx->newservice(service, MSG_TYPE_JSON, data, args.size());
			assert(handle > 1);

			std::string launchinfo = service + "," + args.substr(1, args.size()-2);
			
			service = service=="Snlua"? array[0].GetString() : service;

			assert(online_.insert({handle,  launchinfo}).second);
			serviceHandleToModule_.insert({handle, service});
			serviceModuleToHandle_.insert({service, handle});
		}
		assert(online_.insert({ctx->handle(),  "Launcher,..."}).second);

		uint32_t id = ctx->newport("sig", 0, NULL, 0);
		assert(id > 0);
		ctx->env().sigid = id;
		log.info("Launch Launcher");

	}
	void release(ContextPtr ctx, MessagePtr& message) override
	{
	}

	bool receive(ContextPtr ctx, MessagePtr& message) override
	{
		std::string msg(static_cast<char*>(message->data), message->size);
		if (message->type != MSG_TYPE_JSON) {
			log.warn("Launcher receive message(not json) : " + msg);
			return true;
		}
		
		Document document;
		document.Parse(static_cast<char*>(message->data), message->size);
		Document::AllocatorType& allocator = document.GetAllocator();
		if (!document.IsArray()) {
			log.warn("Launcher receive message(not array): " + msg);
			return true;
		}
		//log.info("Launcher receive message from source(" + u32_to_string(message->source) + ") : " + msg);
		Value array = document.GetArray();
		assert(array.Size() >= 4);
		assert(array[0].IsString()); // pattern: call, cast
		assert(array[1].IsString()); // ref
		assert(array[2].IsInt());    // source
		assert(array[3].IsString()); // function name

		std::string pattern  = array[0].GetString();
		std::string ref      = array[1].GetString();
		uint32_t source      = array[2].GetInt();
		std::string funcname = array[3].GetString();

		auto it = commands_.find(funcname);
		if (it == commands_.end()) {
			log.warn("no find func: " + funcname);
			return true;
		} 
		auto func = it->second;

		Value param(kArrayType);     // function param

		for (uint32_t i=4; i<array.Size(); i++) {
			param.PushBack(array[i], allocator);
		}

		Value returnarray(kArrayType);
		Value rets = func(ctx, param);
		if (pattern == "call") {
			returnarray.PushBack("resp", allocator);
			returnarray.PushBack(array[1], allocator);
			returnarray.PushBack(ctx->handle(), allocator);
			for (uint32_t i=0; i<rets.Size(); i++) {
				returnarray.PushBack(rets[i], allocator);
			}
			StringBuffer buffer;
			Writer<rapidjson::StringBuffer> writer(buffer);
			returnarray.Accept(writer);
			std::string returnstring = buffer.GetString();

			char* data = (char*)malloc(returnstring.size());
			memcpy(data, returnstring.data(), returnstring.size());
			
			auto msg     = ctx->makeMessage();
			msg->type    = MSG_TYPE_JSON;
			msg->data    = data;
			msg->size    = returnstring.size();
			ctx->send(source, msg);
		}

		return true;
	}

	Value serviceLaunch(ContextPtr ctx, const Value& param)
	{
		Value rets(kArrayType);
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();
		
		assert(param.Size() > 0);
		assert(param[0].IsString());
		std::string service = param[0].GetString();
		
		Value serviceparam(kArrayType);
		for (uint32_t i=1; i<param.Size(); i++) {
			Value value;
			value.CopyFrom(param[i], allocator);
			serviceparam.PushBack(value, allocator);
		}

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		serviceparam.Accept(writer);
		std::string args = buffer.GetString();

		char* data = (char*)malloc(args.size());
		memcpy(data, args.data(), args.size());

		uint32_t handle = ctx->newservice(service, MSG_TYPE_JSON, data, args.size());
		assert(handle > 1);
		
		std::string launchinfo = service + "," + args.substr(1, args.size()-2);
		assert(online_.insert({handle, launchinfo}).second);
		//log.info("launch handle(" + u32_to_string(handle) + ") " + launchinfo);
		rets.PushBack(handle, allocator);

		return rets;
	}

	Value serviceRegister(ContextPtr ctx, const Value& param)
	{
		Value rets(kArrayType);
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();
		
		assert(param.Size() == 2);
		assert(param[0].IsInt());
		assert(param[1].IsString());
		uint32_t handle     = param[0].GetInt();
		std::string service = param[1].GetString();
		if (online_.find(handle) == online_.end()) {
			rets.PushBack(false, allocator);
			rets.PushBack("service does not exist", allocator);
			return rets;
		}
		if (serviceHandleToModule_.find(handle) != serviceHandleToModule_.end()) {
			rets.PushBack(false, allocator);
			rets.PushBack("service registered", allocator);
			return rets;
		}
		if (serviceModuleToHandle_.find(service) != serviceModuleToHandle_.end()){
			rets.PushBack(false, allocator);
			rets.PushBack("name is already registered", allocator);
			return rets;
		}
		serviceHandleToModule_.insert({handle, service});
		serviceModuleToHandle_.insert({service, handle});
		rets.PushBack(true, allocator);
		
		return rets;
	}

	Value serviceQuery(ContextPtr ctx, const Value& param)
	{
		Value rets(kArrayType);
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();

		assert(param.Size() == 1);
		assert(param[0].IsString());
		std::string service = param[0].GetString();
		
		auto itm = serviceModuleToHandle_.find(service);
		if (itm != serviceModuleToHandle_.end()) {
			auto ith = serviceHandleToModule_.find(itm->second);
			assert(ith != serviceHandleToModule_.end());
			assert(itm->first == ith->second);
			rets.PushBack(itm->second, allocator);
		}
		return rets;
	}

	Value serviceKill(ContextPtr ctx, const Value& param)
	{
		Value rets(kArrayType);
		// Document document;
		// Document::AllocatorType& allocator = document.GetAllocator();

		assert(param.Size() == 1);
		assert(param[0].IsInt());
		uint32_t handle = param[0].GetInt();

		if (online_.find(handle) == online_.end()) {
			//log.info("kill handle(" + u32_to_string(handle) + ") fail [reason: no find service]");
		} else {
			auto msg  = ctx->makeMessage();
			msg->type = MSG_TYPE_EXIT;
			ctx->send(handle, msg, 1);
			//log.info("kill handle(" + u32_to_string(handle) + ") succeed");
		}
		return rets;
	}

	Value serviceExit(ContextPtr ctx, const Value& param)
	{
		Value rets(kArrayType);
		// Document document;
		// Document::AllocatorType& allocator = document.GetAllocator();

		assert(param.Size() == 1);
		assert(param[0].IsInt());
		uint32_t handle = param[0].GetInt();

		auto it = online_.find(handle);
		if (it != online_.end()) {
			auto ith = serviceHandleToModule_.find(handle);
			if (ith != serviceHandleToModule_.end()) {
				std::string service = ith->second;
				assert(serviceModuleToHandle_.find(service) != serviceModuleToHandle_.end());
				serviceModuleToHandle_.erase(service);
				serviceHandleToModule_.erase(handle);
			}
			online_.erase(handle);
			//log.info("handle(" + u32_to_string(handle) + ") exit");
		}
		auto ht = handleToPort_.find(handle);
		if (ht != handleToPort_.end()) {
			// log.info("port close");
			for (uint32_t id : handleToPort_[handle]) {
				//log.info("handle(" + u32_to_string(handle) + ") port close - " + u32_to_string(id));
				auto msg = ctx->makeMessage();
				msg->source = id;
				msg->type = MSG_TYPE_EXIT;
				ctx->command(id, msg);
			}
			handleToPort_.erase(handle);
		}

		return rets;
	}
	Value serviceOnline(ContextPtr ctx, const Value& param)
	{
		Value rets(kArrayType);
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();

		assert(param.Size() == 0);

		for (auto m : online_) {
			Value serviceparam;
			serviceparam.SetString(m.second.c_str(),m.second.length(), allocator);
			// log.info(u32_to_string(m.first) + "|" + m.second);
			Value contact(kObjectType);
			contact.AddMember("handle", m.first, allocator);
			contact.AddMember("param", serviceparam, allocator);

			rets.PushBack(contact, allocator);
		}
		return rets;
	}
	Value serviceInfo(ContextPtr ctx, const Value& param)
	{
		Value rets(kArrayType);
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();

		assert(param.Size() == 1);
		assert(param[0].IsInt());
		uint32_t handle = param[0].GetInt();

		std::string serviceparam;
		std::string servicename;
		std::string serviceport;

		auto it = online_.find(handle);
		if (it != online_.end()) {
			serviceparam = online_[handle];
		}
		if (serviceHandleToModule_.find(handle) != serviceHandleToModule_.end()) {
			servicename = serviceHandleToModule_[handle];
		}
		if (handleToPort_.find(handle) != handleToPort_.end()) {
			for (uint32_t id : handleToPort_[handle]) {
				if (serviceport.empty()) {
					serviceport += u32_to_string(id);
				} else {
					serviceport += ",";
					serviceport += u32_to_string(id);
				}	
			}
		}

		Value jparam;
		jparam.SetString(serviceparam.data(), serviceparam.size(), allocator);
		
		Value jname;
		jname.SetString(servicename.data(), servicename.size(), allocator);
		
		Value jport;
		jport.SetString(serviceport.data(), serviceport.size(), allocator);
		
		Value contact(kObjectType);
		contact.AddMember("param", jparam, allocator);
		contact.AddMember("name", jname, allocator);
		contact.AddMember("port", jport, allocator);
		rets.PushBack(contact, allocator);

		return rets;
	}
	
	Value portCtl(ContextPtr ctx, const Value& param)
	{
		Value rets(kArrayType);
		assert(param.Size() == 3);
		assert(param[0].IsInt());
		assert(param[1].IsInt());
		assert(param[2].IsInt());
		uint32_t id = param[0].GetInt();
		uint32_t oldOwner = param[1].GetInt();
		uint32_t newOwner = param[2].GetInt();
		// log.warn("portctl," + u32_to_string(id)+","+u32_to_string(oldOwner)+","+u32_to_string(newOwner));
		
		if (oldOwner == newOwner) {
			return rets;
		}

		if (oldOwner != 0) {
			// log.warn("portctl, old 1");
			if (handleToPort_.find(oldOwner) != handleToPort_.end()) {
				handleToPort_[oldOwner].erase(id);
				if (handleToPort_[oldOwner].size() == 0) {
					// log.warn("portctl, old 1-1");
					handleToPort_.erase(oldOwner);
				}
			}
		}

		if (newOwner != 0) {
			// log.warn("portctl, new 2");
			if (online_.find(newOwner) != online_.end()) {
				// log.warn("portctl, new 2-1");
				handleToPort_[newOwner].insert(id);
			} else {
				// log.warn("portctl, new 2-2");
				auto msg = ctx->makeMessage();
				msg->source = id;
				msg->type = MSG_TYPE_EXIT;
				ctx->command(id, msg);
			}
		}
		// std::string sss;
		// for (auto s : handleToPort_) {
		// 	sss += "[" + u32_to_string(s.first) + ":";
		// 	for (auto p : s.second) {
		// 		sss += "," + u32_to_string(p); 
		// 	}
		// 	sss += "]";
		// }
		// log.warn(sss);

		return rets;
	}
	Value portKill(ContextPtr ctx, const Value& param)
	{
		Value rets(kArrayType);
		assert(param.Size() == 2);
		assert(param[0].IsInt());
		assert(param[1].IsInt());
		uint32_t handle = param[0].GetInt();
		uint32_t id = param[1].GetInt();
	
		auto it = online_.find(handle);
		if (it == online_.end()) {
			return rets;
		}
		auto jt = handleToPort_.find(handle);
		if (jt == handleToPort_.end()) {
			return rets;
		}
		auto pt = handleToPort_[handle].find(id);
		if (pt == handleToPort_[handle].end()) {
			return rets;
		}
		handleToPort_[handle].erase(id);
		if (handleToPort_[handle].size() == 0) {
			handleToPort_.erase(handle);
		}
		auto msg = ctx->makeMessage();
		msg->source = id;
		msg->type = MSG_TYPE_EXIT;
		ctx->command(id, msg);

		// std::string sss;
		// for (auto s : handleToPort_) {
		// 	sss += "[" + u32_to_string(s.first) + ":";
		// 	for (auto p : s.second) {
		// 		sss += "," + u32_to_string(p); 
		// 	}
		// 	sss += "]";
		// }
		// log.warn(sss);

		return rets;
	}





	void launchLogger(ContextPtr ctx, rapidjson::Value& m)
	{
		assert(m.IsObject());
		assert(m.HasMember("service"));
		assert(m.HasMember("args"));
		assert(m["service"].IsString());
		assert(m["args"].IsObject());
		
		std::string service    = m["service"].GetString();
		rapidjson::Value param = m["args"].GetObject();

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		param.Accept(writer);
		std::string args = buffer.GetString();

		char* data = (char*)malloc(args.size());
		memcpy(data, args.data(), args.size());

		uint32_t handle = ctx->newservice(service, MSG_TYPE_JSON, data, args.size());
		assert(handle > 1);

		std::string launchinfo = service + "," + args;
		
		assert(online_.insert({handle,  launchinfo}).second);
		serviceHandleToModule_.insert({handle, service});
		serviceModuleToHandle_.insert({service, handle});

		ctx->env().loghandle = handle;
		log.ctx    = ctx.get();
		log.handle = handle;
	}
private:
	std::map<uint32_t,std::string> online_;
	std::map<uint32_t,std::set<uint32_t>> handleToPort_;

	std::map<std::string, uint32_t> serviceModuleToHandle_;
	std::map<uint32_t, std::string> serviceHandleToModule_;
	std::map<std::string, std::function<Value(ContextPtr,const Value&)>> commands_;
	Log log;
};

module(Launcher)
