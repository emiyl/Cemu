#include "Fiber.h"

#include <atomic>
#include <boost/context/detail/fcontext.hpp>
#include <cstdint>
#include <cstdlib>

thread_local Fiber* sCurrentFiber{};

namespace
{
	using boost::context::detail::fcontext_t;
	using boost::context::detail::transfer_t;

	struct FiberContextData
	{
		fcontext_t fctx{};
		void (*entryPoint)(void*){};
		void* userParam{};
	};

	thread_local FiberContextData* sCurrentFiberCtx{};

	void FiberTrampoline(transfer_t transfer)
	{
		auto* callerCtx = static_cast<FiberContextData*>(transfer.data);
		if (callerCtx)
			callerCtx->fctx = transfer.fctx;

		auto* currentCtx = sCurrentFiberCtx;
		currentCtx->entryPoint(currentCtx->userParam);

		cemu_assert_debug(false);
		std::abort();
	}
}

Fiber::Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData) : m_privateData(privateData)
{
	auto* ctx = new FiberContextData();
	ctx->entryPoint = FiberEntryPoint;
	ctx->userParam = userParam;

	const size_t stackSize = 2 * 1024 * 1024;
	m_stackPtr = std::malloc(stackSize);
	if (!m_stackPtr)
	{
		delete ctx;
		std::abort();
	}

	void* stackTop = static_cast<uint8_t*>(m_stackPtr) + stackSize;
	ctx->fctx = boost::context::detail::make_fcontext(stackTop, stackSize, FiberTrampoline);
	m_implData = ctx;
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
	m_implData = new FiberContextData();
	m_stackPtr = nullptr;
}

Fiber::~Fiber()
{
	if (m_stackPtr)
		std::free(m_stackPtr);
	delete static_cast<FiberContextData*>(m_implData);
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	cemu_assert_debug(sCurrentFiber == nullptr);
	sCurrentFiber = new Fiber(privateData);
	sCurrentFiberCtx = static_cast<FiberContextData*>(sCurrentFiber->m_implData);
	return sCurrentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
	auto* leavingCtx = static_cast<FiberContextData*>(sCurrentFiber->m_implData);
	auto* targetCtx = static_cast<FiberContextData*>(targetFiber.m_implData);

	sCurrentFiber = &targetFiber;
	sCurrentFiberCtx = targetCtx;
	std::atomic_thread_fence(std::memory_order_seq_cst);
	transfer_t transfer = boost::context::detail::jump_fcontext(targetCtx->fctx, leavingCtx);
	targetCtx->fctx = transfer.fctx;
	sCurrentFiberCtx = leavingCtx;
	std::atomic_thread_fence(std::memory_order_seq_cst);
}

void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}
