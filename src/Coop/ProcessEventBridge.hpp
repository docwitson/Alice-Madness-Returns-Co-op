#pragma once

#include <cstdint>

class UObject;
class UFunction;

namespace AliceCoop::ProcessEventBridge
{
	enum class Disposition
	{
		Continue,
		Suppress,
		Defer
	};

	enum class DecisionReason
	{
		None,
		HostProgressionTrophy,
		SharedPlayerDamage,
		SequenceActivationBarrier,
		Count
	};

	struct Decision
	{
		Disposition disposition = Disposition::Continue;
		DecisionReason reason = DecisionReason::None;
	};

	class Invocation
	{
	public:
		Invocation(int object, UFunction* function, int params, int result);
		~Invocation();

		Invocation(const Invocation&) = delete;
		Invocation& operator=(const Invocation&) = delete;
		Invocation(Invocation&&) = delete;
		Invocation& operator=(Invocation&&) = delete;

		void SetDecision(Decision decision);

		int RawObject() const;
		UFunction* Function() const;
		int RawParams() const;
		int RawResult() const;

		UObject* Object() const;
		const void* Params() const;

	private:
		friend void AfterOriginal(Invocation& invocation);

		int object_ = 0;
		UFunction* function_ = nullptr;
		int params_ = 0;
		int result_ = 0;
		Decision decision_{};
		bool decisionSet_ = false;
		bool afterOriginalCompleted_ = false;
		bool diagnosticsActive_ = false;
		std::uint32_t diagnosticDepth_ = 0;
	};

	void Initialize();

	[[nodiscard]] Decision EarlyBefore(Invocation& invocation);
	void BeforeOriginal(Invocation& invocation);
	void AfterOriginal(Invocation& invocation);
}
