
/**
* Reachability analysis state holds information about suspended worker contexts.
*/
class FReachabilityAnalysisState
{
public:

	// Work-stealing algorithms are O(N^2), everyone steals from everyone.
	// Might want to improve that before going much wider.
	static constexpr int32 MaxWorkers = 16;

private:
	/** Number of workers threads */
	int32 NumWorkers = 0;
	/** Worker thread contexts */
	FWorkerContext* Contexts[MaxWorkers] = { nullptr };

	/** Additional object flags for rooting UObjects reachability analysis was kicked off with */
	EObjectFlags ObjectKeepFlags = RF_NoFlags;
	/** True if reachability analysis started as part of full GC */
	bool bPerformFullPurge = false;
	/** True if reachability analysis was paused due to time limit */
	bool bIsSuspended = false;
	/** Stats of the last reachability analysis run (all iterations) */
	UE::GC::FProcessorStats Stats;

	/** Total time of all reachability analysis iterations */
	double IncrementalMarkPhaseTotalTime = 0.0;

	/** Total time of the actual reference traversal */
	double ReferenceProcessingTotalTime = 0.0;

	/** Number of reachability analysis iterations performed during reachability analysis */
	int32 NumIterations = 0;

	/** Number of reachability analysis iterations to skip when running with gc.DelayReachabilityIterations */
	int32 NumRechabilityIterationsToSkip = 0;

	alignas (PLATFORM_CACHE_LINE_SIZE) double IterationStartTime = 0.0;
	alignas (PLATFORM_CACHE_LINE_SIZE) double IterationTimeLimit = 0.0;

public:

	FReachabilityAnalysisState() = default;

	void StartTimer(double InTimeLimit)
	{
		IterationTimeLimit = InTimeLimit;
		IterationStartTime = FPlatformTime::Seconds();
	}

	const UE::GC::FProcessorStats& GetStats() const
	{
		return Stats;
	}

	bool IsSuspended() const
	{
		return bIsSuspended;
	}

	/** Kicks off new Garbage Collection cycle */
	void CollectGarbage(EObjectFlags KeepFlags, bool bFullPurge);

	/** Performs Reachability Analysis (also incrementally) and destroys objects (also incrementally) */
	void PerformReachabilityAnalysisAndConditionallyPurgeGarbage(bool bReachabilityUsingTimeLimit);

	/** Checks if Time Limit has been exceeded. This function needs to be super fast as it's called very frequently. */
	FORCEINLINE bool IsTimeLimitExceeded() const
	{
		if (IterationTimeLimit > 0.0)
		{
			return (FPlatformTime::Seconds() - IterationStartTime) >= IterationTimeLimit;
		}
		return false;
	}

	FORCEINLINE int32 GetNumWorkers() const
	{
		return NumWorkers;
	}

	FORCEINLINE FWorkerContext** GetContextArray()
	{
		return Contexts;
	}

	FORCEINLINE int32 GetNumIterations() const
	{
		return NumIterations;
	}

	/** Initializes reachability analysis */
	void Init();

	/** Initializes expected number of worker threads */
	void SetupWorkers(int32 InNumWorkers);

	/** Main Garbage Collection function executed on Reachability Analysis Thread when StartEvent has been triggered */
	void PerformReachabilityAnalysis();

	/** Updates reachability analysis state after RA iteration */
	void UpdateStats(const UE::GC::FProcessorStats& InStats);

	/** Checks if any of the active worker contexts is suspended and updates the IsSuspended state */
	bool CheckIfAnyContextIsSuspended();

	/** Resets workers after reachability analysis is fully complete */
	void ResetWorkers();

	/** Marks the end of reachability iteration */
	void FinishIteration();
};