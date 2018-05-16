#pragma once

#include "QGrpcBase.h"

using grpc::Channel;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerContext;
using grpc::ServerAsyncReader;
using grpc::ServerAsyncWriter;
using grpc::ServerAsyncReaderWriter;
using grpc::CompletionQueue;
using grpc::ServerCompletionQueue;
using grpc::Status;

namespace QGrpcSrvBase
{

	/*struct callCounter
	{
		size_t unary_finish_call;
		size_t server_stream_finish_call;
		size_t client_stream_finish_call;
		size_t bidi_stream_finish_call;
		static callCounter& get()
		{
			static callCounter cc;
			return cc;
		}
	private:
		explicit callCounter():
			unary_finish_call(0),
			server_stream_finish_call(0),
			client_stream_finish_call(0),
			bidi_stream_finish_call(0)
		{}
	};*/


	struct RPC_KIND_UNARY_T;
	struct RPC_KIND_SERVERSTREAMING_T;
	struct RPC_KIND_CLIENTSTREAMING_T;
	struct RPC_KIND_BIDISTREAMING_T;


	template<typename RequestType, typename ReplyType>
	struct ServerCallDataResponse
	{
		RequestType request;
		ReplyType reply;
	};


	template< template<typename... Args> class R, typename...Args> struct ServerResponderBase
	{
		using responder_type = R< Args...>;
		ServerContext context;
		Status status;
		ServerResponderBase(): responder(&context), couldBeDeleted_(false), tag_(nullptr) {}
		virtual ~ServerResponderBase() {}
		bool CouldBeDeleted() const { return couldBeDeleted_; };
		inline std::string peer() const { return context.peer(); }
	protected:
		responder_type responder; //must be deleted before context
		bool couldBeDeleted_;
		void* tag_;
		virtual bool processEvent(void*, bool) = 0;
	};

	template<typename KIND, typename ReplyType, typename RequestType> struct ServerResponder;
	template<typename ReplyType, typename RequestType>
	struct ServerResponder<RPC_KIND_UNARY_T, ReplyType, RequestType> : public ServerResponderBase< ServerAsyncResponseWriter, ReplyType >, public ServerCallDataResponse<RequestType, ReplyType>
	{
		virtual ~ServerResponder() {}
		inline void Finish() 
		{ 
			this->couldBeDeleted_ = true; 
			this->responder.Finish(this->reply, this->status, this->tag_);
		}
	protected:
		virtual bool processEvent(void* tag, bool) override { this->tag_ = tag; return true; }
	};

	template<typename ReplyType, typename RequestType>
	struct ServerResponder<RPC_KIND_SERVERSTREAMING_T, ReplyType, RequestType> : public ServerResponderBase< ServerAsyncWriter, ReplyType >, public ServerCallDataResponse<RequestType, ReplyType>
	{
		ServerResponder() : isFinished_(false) {}
		virtual ~ServerResponder() {}
		inline void Write() { return this->responder.Write(this->reply, this->tag_); }
		inline void Finish() { this->responder.Finish(this->status, this->tag_); this->couldBeDeleted_ = true; }
	protected:
		virtual bool processEvent(void* tag, bool/* ok*/) override { this->tag_ = tag; return true; }
		bool isFinished_;
	};

	template<typename ReplyType, typename RequestType>
	struct ServerResponder<RPC_KIND_CLIENTSTREAMING_T, ReplyType, RequestType> : public ServerResponderBase< ServerAsyncReader, ReplyType, RequestType >, public ServerCallDataResponse<RequestType, ReplyType>
	{
		ServerResponder() : read_mode_(true) {}
		inline void Read() { return this->responder.Read(&this->request, this->tag_); }
		inline bool StreamFinished() const { return !this->read_mode_; }
		inline void Finish() { this->couldBeDeleted_ = true; return this->responder.Finish(this->reply, this->status, this->tag_); }
	protected:
		virtual bool processEvent(void* tag, bool ok) override
		{
			this->tag_ = tag;
			read_mode_ = ok;
			return true;
		}
		bool read_mode_;
	};

	template<typename ReplyType, typename RequestType>
	struct ServerResponder<RPC_KIND_BIDISTREAMING_T, ReplyType, RequestType> : public ServerResponderBase< ServerAsyncReaderWriter, ReplyType, RequestType >, public ServerCallDataResponse<RequestType, ReplyType>
	{
		ServerResponder() : write_mode_(false), time_to_finish_(false) {}
		inline bool WriteMode() const { return write_mode_; }
		inline void Write() { return this->responder.Write(this->reply, this->tag_); }
		inline void Finish() { this->couldBeDeleted_ = true; return this->responder.Finish(this->status, this->tag_); }
		inline bool TimeToFinish() const { return this->time_to_finish_; }
		inline void Read() { return this->responder.Read(&this->request, this->tag_); }
	protected:
		virtual bool processEvent(void* tag, bool ok) override
		{
			this->tag_ = tag;
			if (!ok)
			{
				if (!write_mode_) write_mode_ = true;
				else time_to_finish_ = true;
			}
			return true;
		}
		bool write_mode_;
		bool time_to_finish_;
	};


	inline std::chrono::system_clock::time_point deadlineFromSec(long long seconds) { return std::chrono::system_clock::now() + std::chrono::seconds(seconds); }
	inline std::chrono::system_clock::time_point deadlineFromMSec(long long mseconds) { return std::chrono::system_clock::now() + std::chrono::milliseconds(mseconds); }

	//class QGrpcServerService;


	template<typename KIND, typename REQUEST, typename REPLY, typename ASYNCGRPCSERVICE, typename RPCREQUESTFUNCTYPE, typename RESPONDERTYPE>
	typename std::enable_if<std::is_same<KIND, RPC_KIND_UNARY_T>::value>::type RequestRPC(ASYNCGRPCSERVICE* service, RPCREQUESTFUNCTYPE requestFunc, grpc::ServerContext* ctx, REQUEST* request, RESPONDERTYPE* responder, grpc::CompletionQueue* call_cq, grpc::ServerCompletionQueue* notification_cq, void* tag)
	{
		return (service->*requestFunc)(ctx, request, responder, call_cq, notification_cq, tag);
	}

	template<typename KIND, typename REQUEST, typename REPLY, typename ASYNCGRPCSERVICE, typename RPCREQUESTFUNCTYPE, typename RESPONDERTYPE>
	typename std::enable_if<std::is_same<KIND, RPC_KIND_SERVERSTREAMING_T>::value>::type RequestRPC(ASYNCGRPCSERVICE* service, RPCREQUESTFUNCTYPE requestFunc, grpc::ServerContext* ctx, REQUEST* request, RESPONDERTYPE* responder, grpc::CompletionQueue* call_cq, grpc::ServerCompletionQueue* notification_cq, void* tag)
	{
		return (service->*requestFunc)(ctx, request, responder, call_cq, notification_cq, tag);
	}

	template<typename KIND, typename REQUEST, typename REPLY, typename ASYNCGRPCSERVICE, typename RPCREQUESTFUNCTYPE, typename RESPONDERTYPE>
	typename std::enable_if<std::is_same<KIND, RPC_KIND_CLIENTSTREAMING_T>::value>::type RequestRPC(ASYNCGRPCSERVICE* service, RPCREQUESTFUNCTYPE requestFunc, grpc::ServerContext* ctx, REQUEST* /*request*/, RESPONDERTYPE* responder, grpc::CompletionQueue* call_cq, grpc::ServerCompletionQueue* notification_cq, void* tag)
	{
		return (service->*requestFunc)(ctx, responder, call_cq, notification_cq, tag);
	}

	template<typename KIND, typename REQUEST, typename REPLY, typename ASYNCGRPCSERVICE, typename RPCREQUESTFUNCTYPE, typename RESPONDERTYPE>
	typename std::enable_if<std::is_same<KIND, RPC_KIND_BIDISTREAMING_T>::value>::type RequestRPC(ASYNCGRPCSERVICE* service, RPCREQUESTFUNCTYPE requestFunc, grpc::ServerContext* ctx, REQUEST* /*request*/, RESPONDERTYPE* responder, grpc::CompletionQueue* call_cq, grpc::ServerCompletionQueue* notification_cq, void* tag)
	{
		return (service->*requestFunc)(ctx, responder, call_cq, notification_cq, tag);
	}


    class QGrpcServerService;

	struct AbstractCallData
	{
		virtual void cqReaction(const QGrpcServerService*, bool, bool) = 0;
		virtual void Destroy() = 0;
	};


	class QGrpcServerService : public QGrpcBase::AbstractService
	{
		grpc::Service* service_;
		bool started_;
		std::string server_address_;
		std::unique_ptr<Server> server_;
		std::unique_ptr<ServerCompletionQueue> notification_cq;
	public:
		inline void start_service(const std::string& addr_uri)
		{
			server_address_ = addr_uri;
			assert(!server_address_.empty()); assert(service_);
			ServerBuilder builder;
			builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
			builder.RegisterService(service_);
			notification_cq = builder.AddCompletionQueue();
			server_ = builder.BuildAndStart();
			started_ = true;
			makeRequests();
		}

		explicit QGrpcServerService(grpc::Service* service): service_(service), started_(false){}
		virtual ~QGrpcServerService()
		{
			started_ = false;
			if (server_)
			{
				server_->Shutdown(/*std::chrono::system_clock::time_point()*/);
				server_->Wait();
			}
			if (notification_cq)
			{
				notification_cq->Shutdown(); // Always after the associated server's Shutdown()! 
											 // Drain the cq_ that was created 
				void* ignored_tag; bool ignored_ok;
				while (notification_cq->Next(&ignored_tag, &ignored_ok)) {}
			}
			for (auto cd : cdatas_)
				cd->Destroy();
			server_ = nullptr;
			notification_cq = nullptr;
		}

		virtual void checkCQ()
		{
			if (!started_) return;
			void* tag;
			bool ok = false;
			grpc::CompletionQueue::NextStatus st;
			st = notification_cq->AsyncNext(&tag, &ok, std::chrono::system_clock::time_point());
			if ((st == grpc::CompletionQueue::SHUTDOWN) || (st == grpc::CompletionQueue::TIMEOUT))/* || (st != grpc::CompletionQueue::GOT_EVENT) || !ok)*/
				return;
			static_cast<QGrpcSrvBase::AbstractCallData*>(tag)->cqReaction(this, ok, !started_);
			return;
		}

		template<typename RPCTypes, typename RPCCallData>
		void needAnotherCallData()
		{
			RPCCallData* cd = new RPCCallData();
			cdatas_.insert(cd);
			QGrpcSrvBase::RequestRPC<typename RPCTypes::kind, typename RPCTypes::RequestType, typename RPCTypes::ReplyType, typename RPCTypes::AsyncGrpcServiceType, typename RPCTypes::RPCRequestFuncType, typename RPCCallData::responder_type>
				(dynamic_cast<typename RPCTypes::AsyncGrpcServiceType*>(service_), cd->request_func_, &cd->context, &cd->request, &cd->responder, this->notification_cq.get(), this->notification_cq.get(), (void*)cd);
		}

		void destroyCallData(const AbstractCallData* cd)
		{
			assert(cdatas_.count(const_cast<AbstractCallData*>(cd)));
			cdatas_.erase(const_cast<AbstractCallData*>(cd));
			const_cast<AbstractCallData*>(cd)->Destroy();
		}
	protected:
		virtual void makeRequests() = 0;
	private:
		std::set<AbstractCallData*> cdatas_;

	};


	template<typename RPC, typename RPCCallData>
	class ServerCallData : public AbstractCallData, public ServerResponder< typename RPC::kind, typename RPC::ReplyType, typename RPC::RequestType >
	{
		using SignalType = void (RPC::ServiceType::*)( RPCCallData* );
		using RPCRequestType = typename RPC::RPCRequestFuncType;
		SignalType signal_func_;
		RPCRequestType request_func_;
		bool first_time_reaction_;
		virtual void Destroy() override 
		{
			auto response = dynamic_cast<RPCCallData*>(this);
			delete response;
		}

	public:
		explicit ServerCallData(SignalType signal_func, RPCRequestType request_func) :signal_func_(signal_func), request_func_(request_func), first_time_reaction_(false) {}
		virtual ~ServerCallData() {}
		inline virtual void cqReaction(const QGrpcServerService* service_, bool ok, bool server_shutdown) override
		{
			auto response = dynamic_cast<RPCCallData*>(this);
			if (server_shutdown)
			{
				(const_cast<QGrpcServerService*>(service_))->destroyCallData(this);
				return;
			}
			if (!first_time_reaction_)
			{
				first_time_reaction_ = true;
				(const_cast<QGrpcServerService*>(service_))->needAnotherCallData<RPC, RPCCallData>();
			}
			void* tag = static_cast<void*>(response);
			if (response->CouldBeDeleted())
			{
				(const_cast<QGrpcServerService*>(service_))->destroyCallData(this);
				return;
			}
			if (!this->processEvent(tag, ok)) return;
			((const_cast<typename RPC::ServiceType*>(dynamic_cast<const typename RPC::ServiceType*>(service_)))->*signal_func_)( response );
		}
		friend typename RPC::ServiceType;
		friend QGrpcServerService;
	};




	template<typename KIND, typename REQUEST, typename REPLY, typename ASYNCGRPCSERVICE>
	struct RPCRequestFunc_;

	template<typename REQUEST, typename REPLY, typename ASYNCGRPCSERVICE>
	struct RPCRequestFunc_<RPC_KIND_UNARY_T, REQUEST, REPLY, ASYNCGRPCSERVICE>
	{
		using RequestFuncType = void(ASYNCGRPCSERVICE::*)(
			grpc::ServerContext*, 
			REQUEST*, 
			typename ServerResponder<RPC_KIND_UNARY_T,REPLY, REQUEST>::responder_type*,
			grpc::CompletionQueue*, 
			grpc::ServerCompletionQueue*, 
			void *);
	};

	template<typename REQUEST, typename REPLY, typename ASYNCGRPCSERVICE>
	struct RPCRequestFunc_<RPC_KIND_SERVERSTREAMING_T, REQUEST, REPLY, ASYNCGRPCSERVICE>
	{
		using RequestFuncType = void(ASYNCGRPCSERVICE::*)(
			grpc::ServerContext*,
			REQUEST*,
			typename ServerResponder<RPC_KIND_SERVERSTREAMING_T, REPLY, REQUEST>::responder_type*,
			grpc::CompletionQueue*,
			grpc::ServerCompletionQueue*,
			void *);
	};

	template<typename REQUEST, typename REPLY, typename ASYNCGRPCSERVICE>
	struct RPCRequestFunc_<RPC_KIND_CLIENTSTREAMING_T, REQUEST, REPLY, ASYNCGRPCSERVICE>
	{
		using RequestFuncType = void(ASYNCGRPCSERVICE::*)(
			grpc::ServerContext*,
			typename ServerResponder<RPC_KIND_CLIENTSTREAMING_T, REPLY, REQUEST>::responder_type*,
			grpc::CompletionQueue*,
			grpc::ServerCompletionQueue*,
			void *);
	};

	template<typename REQUEST, typename REPLY, typename ASYNCGRPCSERVICE>
	struct RPCRequestFunc_<RPC_KIND_BIDISTREAMING_T, REQUEST, REPLY, ASYNCGRPCSERVICE>
	{
		using RequestFuncType = void(ASYNCGRPCSERVICE::*)(
			grpc::ServerContext*,
			typename ServerResponder<RPC_KIND_BIDISTREAMING_T, REPLY, REQUEST>::responder_type*,
			grpc::CompletionQueue*,
			grpc::ServerCompletionQueue*,
			void *);
	};


	template<typename KIND, typename REQUEST, typename REPLY, typename SERVICE, typename ASYNCGRPCSERVICE> 
	struct RPCtypes
	{
		using kind = KIND;
		using ReplyType = REPLY;
		using RequestType = REQUEST;
		using ServiceType = SERVICE;
		using AsyncGrpcServiceType = ASYNCGRPCSERVICE;
		using RPCRequestFuncType = typename RPCRequestFunc_<KIND, REQUEST, REPLY, ASYNCGRPCSERVICE>::RequestFuncType;
	};


}