class FRealtimeGC : public FGarbageCollectionTracer
{
	typedef void(FRealtimeGC::*ReachabilityAnalysisFn)(FWorkerContext&);

	/** Pointers to functions used for Reachability Analysis */
	ReachabilityAnalysisFn ReachabilityAnalysisFunctions[8];

	UE::Tasks::TTask<void> InitialCollection;
	TArray<UObject**> InitialReferences;
	TArray<UObject*> InitialObjects;

	void BeginInitialReferenceCollection(EGCOptions Options)
	{
		InitialReferences.Reset();

		if (IsParallel(Options))
		{
			InitialCollection = UE::Tasks::Launch(TEXT("CollectInitialReferences"), 
				[&] () { FGCObject::GGCObjectReferencer->AddInitialReferences(InitialReferences); });
		}
	}

	TConstArrayView<UObject**> GetInitialReferences(EGCOptions Options)
	{
		if (!!(Options & EGCOptions::Parallel))
		{
			InitialCollection.Wait();
		}
		else
		{
			FGCObject::GGCObjectReferencer->AddInitialReferences(InitialReferences);
		}

		return InitialReferences;
	}

	template<class CollectorType, class ProcessorType>
	FORCEINLINE void CollectReferencesForGC(ProcessorType& Processor, UE::GC::FWorkerContext& Context)
	{
		using FastReferenceCollector = TFastReferenceCollector<ProcessorType, CollectorType>;

		if constexpr (IsParallel(ProcessorType::Options))
		{
			ProcessAsync([](void* P, FWorkerContext& C) { FastReferenceCollector(*reinterpret_cast<ProcessorType*>(P)).ProcessObjectArray(C); }, &Processor, Context);
		}
		else
		{
			FastReferenceCollector(Processor).ProcessObjectArray(Context);
		}
	}

	template <EGCOptions Options>
	void PerformReachabilityAnalysisOnObjectsInternal(FWorkerContext& Context)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PerformReachabilityAnalysisOnObjectsInternal);

#if !UE_BUILD_SHIPPING
		TDebugReachabilityProcessor<Options> DebugProcessor;
		if (DebugProcessor.IsForceEnabled() | //-V792
			DebugProcessor.TracksHistory() | 
			DebugProcessor.TracksGarbage() & Stats.bFoundGarbageRef)
		{
			CollectReferencesForGC<TDebugReachabilityCollector<Options>>(DebugProcessor, Context);
			return;
		}
#endif
		
		TReachabilityProcessor<Options> Processor;
		CollectReferencesForGC<TReachabilityCollector<Options>>(Processor, Context);
	}

	/** Calculates GC function index based on current settings */
	static FORCEINLINE int32 GetGCFunctionIndex(EGCOptions InOptions)
	{
		return (!!(InOptions & EGCOptions::Parallel)) |
			(!!(InOptions & EGCOptions::EliminateGarbage) << 1) |
			(!!(InOptions & EGCOptions::IncrementalReachability) << 2);
	}

public:
	/** Default constructor, initializing all members. */
	FRealtimeGC()
	{
		ReachabilityAnalysisFunctions[GetGCFunctionIndex(EGCOptions::None)] = &FRealtimeGC::PerformReachabilityAnalysisOnObjectsInternal<EGCOptions::None | EGCOptions::None>;
		ReachabilityAnalysisFunctions[GetGCFunctionIndex(EGCOptions::Parallel | EGCOptions::None)] = &FRealtimeGC::PerformReachabilityAnalysisOnObjectsInternal<EGCOptions::Parallel | EGCOptions::None>;

		ReachabilityAnalysisFunctions[GetGCFunctionIndex(EGCOptions::None | EGCOptions::EliminateGarbage)] = &FRealtimeGC::PerformReachabilityAnalysisOnObjectsInternal<EGCOptions::None | EGCOptions::EliminateGarbage>;
		ReachabilityAnalysisFunctions[GetGCFunctionIndex(EGCOptions::Parallel | EGCOptions::EliminateGarbage)] = &FRealtimeGC::PerformReachabilityAnalysisOnObjectsInternal<EGCOptions::Parallel | EGCOptions::EliminateGarbage>;

		ReachabilityAnalysisFunctions[GetGCFunctionIndex(EGCOptions::None | EGCOptions::IncrementalReachability)] = &FRealtimeGC::PerformReachabilityAnalysisOnObjectsInternal<EGCOptions::None | EGCOptions::IncrementalReachability>;
		ReachabilityAnalysisFunctions[GetGCFunctionIndex(EGCOptions::Parallel | EGCOptions::IncrementalReachability)] = &FRealtimeGC::PerformReachabilityAnalysisOnObjectsInternal<EGCOptions::Parallel | EGCOptions::IncrementalReachability>;

		ReachabilityAnalysisFunctions[GetGCFunctionIndex(EGCOptions::EliminateGarbage | EGCOptions::IncrementalReachability)] = &FRealtimeGC::PerformReachabilityAnalysisOnObjectsInternal<EGCOptions::None | EGCOptions::EliminateGarbage | EGCOptions::IncrementalReachability>;
		ReachabilityAnalysisFunctions[GetGCFunctionIndex(EGCOptions::Parallel | EGCOptions::EliminateGarbage | EGCOptions::IncrementalReachability)] = &FRealtimeGC::PerformReachabilityAnalysisOnObjectsInternal<EGCOptions::Parallel | EGCOptions::EliminateGarbage | EGCOptions::IncrementalReachability>;

		FGCObject::StaticInit();
	}

	struct FMarkClustersArrays
	{
		TArray<FUObjectItem*> KeepClusters;
		TArray<FUObjectItem*> ClustersToDissolve;

		inline static bool Reserve(const TArray<TGatherIterator<FMarkClustersArrays>, TInlineAllocator<32>>& InGatherResults, FMarkClustersArrays& OutCombinedResults)
		{
			int32 NumKeepClusters = 0;
			int32 NumClustersToDissolve = 0;
			for (const TGatherIterator<FMarkClustersArrays>& It : InGatherResults)
			{
				NumKeepClusters += It.Payload.KeepClusters.Num();
				NumClustersToDissolve += It.Payload.ClustersToDissolve.Num();
			}
			OutCombinedResults.KeepClusters.Reserve(OutCombinedResults.KeepClusters.Num() + NumKeepClusters);
			OutCombinedResults.ClustersToDissolve.Reserve(OutCombinedResults.ClustersToDissolve.Num() + NumClustersToDissolve);
			return (NumKeepClusters + NumClustersToDissolve) > 0;
		}

		inline static int32 Num(const FMarkClustersArrays& InArrays)
		{
			return InArrays.KeepClusters.Num() + InArrays.ClustersToDissolve.Num();
		}

		inline static void Append(const FMarkClustersArrays& InSource, FMarkClustersArrays& OutDest)
		{
			OutDest.KeepClusters += InSource.KeepClusters;
			OutDest.ClustersToDissolve += InSource.ClustersToDissolve;
		}
	};
	
	FORCENOINLINE void MarkClusteredObjectsAsReachable(const EGatherOptions Options, TArray<UObject*>& OutRootObjects)
	{
		using namespace UE::GC;
		using namespace UE::GC::Private;
		using FMarkClustersState = TThreadedGather<FMarkClustersArrays, FMarkClustersArrays>;

		std::atomic<int32> TotalClusteredObjects = 0;
		FMarkClustersState GatherClustersState;
		TArray<FUObjectCluster>& ClusterArray = GUObjectClusters.GetClustersUnsafe();

		// StartGathering calculates the number of threads based on the number of objects but here the objects are actually clusters
		// that contain many more objects than the number of clusters so we want to be able to process at least two clusters per thread	
		const int32 NumThreads = !!(Options & EGatherOptions::Parallel) ? FMath::Min(GetNumCollectReferenceWorkers(), (ClusterArray.Num() + 1) / 2) : 1;
		GatherClustersState.Start(Options, ClusterArray.Num(), /* FirstIndex = */ 0, NumThreads);
		FMarkClustersState::FThreadIterators& ThreadIterators = GatherClustersState.GetThreadIterators();

		ParallelFor(TEXT("GC.MarkClusteredObjectsAsReachable"), GatherClustersState.NumWorkerThreads(), 1, [&ThreadIterators, &ClusterArray, &TotalClusteredObjects](int32 ThreadIndex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MarkClusteredObjectsAsReachableTask);
			FMarkClustersState::FIterator& ThreadState = ThreadIterators[ThreadIndex];
			int32 ThisThreadClusteredObjects = 0;

			while (ThreadState.Index <= ThreadState.LastIndex)
			{
				int32 ClusterIndex = ThreadState.Index++;
				FUObjectCluster& Cluster = ClusterArray[ClusterIndex];
				if (Cluster.RootIndex >= 0)
				{
					ThisThreadClusteredObjects += Cluster.Objects.Num();

					FUObjectItem* RootItem = &GUObjectArray.GetObjectItemArrayUnsafe()[Cluster.RootIndex];
					if (!RootItem->IsGarbage())
					{
						bool bKeepCluster = RootItem->HasAnyFlags(EInternalObjectFlags_RootFlags);
						if (bKeepCluster)
						{
							RootItem->FastMarkAsReachableInterlocked_ForGC();
							ThreadState.Payload.KeepClusters.Add(RootItem);
						}

						for (int32 ObjectIndex : Cluster.Objects)
						{
							FUObjectItem* ClusteredItem = &GUObjectArray.GetObjectItemArrayUnsafe()[ObjectIndex];

							ClusteredItem->FastMarkAsReachableAndClearReachaleInClusterInterlocked_ForGC();

							if (!bKeepCluster && ClusteredItem->HasAnyFlags(EInternalObjectFlags_RootFlags))
							{
								ThreadState.Payload.KeepClusters.Add(RootItem);
								bKeepCluster = true;
							}
						}
					}
					else
					{
						ThreadState.Payload.ClustersToDissolve.Add(RootItem);
					}
				}
			}
			TotalClusteredObjects += ThisThreadClusteredObjects;
		}, (GatherClustersState.NumWorkerThreads() == 1) ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
	
		FMarkClustersArrays MarkClustersResults;
		GatherClustersState.Finish(MarkClustersResults);

		for (FUObjectItem* ObjectItem : MarkClustersResults.ClustersToDissolve)
		{
			// Check if the object is still a cluster root - it's possible one of the previous
			// DissolveClusterAndMarkObjectsAsUnreachable calls already dissolved its cluster
			if (ObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot))
			{
				GUObjectClusters.DissolveClusterAndMarkObjectsAsUnreachable(ObjectItem);
				GUObjectClusters.SetClustersNeedDissolving();
			}
		}

		for (FUObjectItem* ObjectItem : MarkClustersResults.KeepClusters)
		{
			checkSlow(ObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot));
			// this thing is definitely not marked unreachable, so don't test it here
			// Make sure all referenced clusters are marked as reachable too
			MarkReferencedClustersAsReachable<EGCOptions::None>(ObjectItem->GetClusterIndex(), OutRootObjects);
		}

		GGCStats.NumClusteredObjects = TotalClusteredObjects.load(std::memory_order_acquire);
	}

	FORCENOINLINE void MarkRootObjectsAsReachable(const EGatherOptions Options, const EObjectFlags KeepFlags, TArray<UObject*>& OutRootObjects)
	{
		using namespace UE::GC;
		using namespace UE::GC::Private;		
		using FMarkRootsState = TThreadedGather<TArray<UObject*>>;

		FMarkRootsState MarkRootsState;		

		{
			GRootsCritical.Lock();
			TArray<int32> RootsArray(GRoots.Array());				
			GRootsCritical.Unlock();
			MarkRootsState.Start(Options, RootsArray.Num());
			FMarkRootsState::FThreadIterators& ThreadIterators = MarkRootsState.GetThreadIterators();

			ParallelFor(TEXT("GC.MarkRootObjectsAsReachable"), MarkRootsState.NumWorkerThreads(), 1, [&ThreadIterators, &RootsArray](int32 ThreadIndex)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MarkClusteredObjectsAsReachableTask);
				FMarkRootsState::FIterator& ThreadState = ThreadIterators[ThreadIndex];

				while (ThreadState.Index <= ThreadState.LastIndex)
				{
					FUObjectItem* RootItem = &GUObjectArray.GetObjectItemArrayUnsafe()[RootsArray[ThreadState.Index++]];
					UObject* Object = static_cast<UObject*>(RootItem->Object);

					// IsValidLowLevel is extremely slow in this loop so only do it in debug
					checkSlow(Object->IsValidLowLevel());					
#if DO_GUARD_SLOW
					// We cannot mark Root objects as Garbage.
					checkCode(if (RootItem->HasAllFlags(EInternalObjectFlags::Garbage | EInternalObjectFlags::RootSet)) { UE_LOG(LogGarbage, Fatal, TEXT("Object %s is part of root set though has been marked as Garbage!"), *Object->GetFullName()); });
#endif

					RootItem->FastMarkAsReachableInterlocked_ForGC();
					ThreadState.Payload.Add(Object);
				}
			}, (MarkRootsState.NumWorkerThreads() == 1) ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);			
		}

		using FMarkObjectsState = TThreadedGather<TArray<UObject*>>;
		FMarkObjectsState MarkObjectsState;

		// This is super slow as we need to look through all existing UObjects and access their memory to check EObjectFlags
		if (KeepFlags != RF_NoFlags)
		{
			MarkObjectsState.Start(Options, GUObjectArray.GetObjectArrayNum(), GUObjectArray.GetFirstGCIndex());

			FMarkObjectsState::FThreadIterators& ThreadIterators = MarkObjectsState.GetThreadIterators();
			ParallelFor(TEXT("GC.SlowMarkObjectAsReachable"), MarkObjectsState.NumWorkerThreads(), 1, [&ThreadIterators, &KeepFlags](int32 ThreadIndex)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MarkClusteredObjectsAsReachableTask);
				FMarkObjectsState::FIterator& ThreadState = ThreadIterators[ThreadIndex];
				const bool bWithGarbageElimination = UObject::IsGarbageEliminationEnabled();

				while (ThreadState.Index <= ThreadState.LastIndex)
				{
					FUObjectItem* ObjectItem = &GUObjectArray.GetObjectItemArrayUnsafe()[ThreadState.Index++];
					UObject* Object = static_cast<UObject*>(ObjectItem->Object);
					if (Object &&
						!ObjectItem->HasAnyFlags(EInternalObjectFlags_RootFlags) && // It may be counter intuitive to reject roots but these are tracked with GRoots and have already been marked and added
						!(bWithGarbageElimination && ObjectItem->IsGarbage()) && Object->HasAnyFlags(KeepFlags)) // Garbage elimination works regardless of KeepFlags
					{
						// IsValidLowLevel is extremely slow in this loop so only do it in debug
						checkSlow(Object->IsValidLowLevel());

						ObjectItem->FastMarkAsReachableInterlocked_ForGC();
						ThreadState.Payload.Add(Object);
					}
				}
			}, (MarkObjectsState.NumWorkerThreads() == 1) ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
		}

		// Preallocate the resulting array taking both MarkRootsState and MarkObjectsState results into account to avoild reallocating OutRootObjects in each of the Finish() calls.
		OutRootObjects.Reserve(OutRootObjects.Num() + MarkRootsState.NumGathered() + MarkObjectsState.NumGathered() + ObjectLookahead);
		MarkRootsState.Finish(OutRootObjects);
		MarkObjectsState.Finish(OutRootObjects);

		GGCStats.NumRoots = OutRootObjects.Num();
	}

	/**
	 * Marks all objects that don't have KeepFlags and EInternalObjectFlags_GarbageCollectionKeepFlags as MaybeUnreachable
	 */
	FORCENOINLINE void MarkObjectsAsUnreachable(const EObjectFlags KeepFlags)
	{
		using namespace UE::GC;

		// Don't swap the flags if we're re-entering this function to track garbage references
		if (const bool bInitialMark = !Stats.bFoundGarbageRef)
		{
			// This marks all UObjects as MaybeUnreachable
			Swap(GReachableObjectFlag, GMaybeUnreachableObjectFlag);
		}

		// Not counting the disregard for GC set to preserve legacy behavior
		GObjectCountDuringLastMarkPhase.Set(GUObjectArray.GetObjectArrayNumMinusAvailable() - GUObjectArray.GetFirstGCIndex());

		EGatherOptions GatherOptions = GetObjectGatherOptions();

		// Now make sure all clustered objects and root objects are marked as Reachable. 
		// This could be considered as initial part of reachability analysis and could be made incremental.
		MarkClusteredObjectsAsReachable(GatherOptions, InitialObjects);
		MarkRootObjectsAsReachable(GatherOptions, KeepFlags, InitialObjects);
	}

private:

	void ConditionallyAddBarrierReferencesToHistory(FWorkerContext& Context)
	{
#if !UE_BUILD_SHIPPING && ENABLE_GC_HISTORY
		if (FGCHistory::Get().IsActive())
		{
			static const FName NAME_Barrier = FName(TEXT("Barrier"));

			TArray<FGCDirectReference>& DirectReferences = GetContextHistoryReferences(Context, FReferenceToken(FGCHistory::Get().GetBarrierObject()), InitialObjects.Num());
			for (UObject* BarrierObject : InitialObjects)
			{
				DirectReferences.Add(FGCDirectReference(BarrierObject, NAME_Barrier));
			}
		}
#endif // !UE_BUILD_SHIPPING && ENABLE_GC_HISTORY
	}

	void StartReachabilityAnalysis(EObjectFlags KeepFlags, const EGCOptions Options)
	{
		BeginInitialReferenceCollection(Options);

		// Reset object count.
		GObjectCountDuringLastMarkPhase.Reset();
		
		InitialObjects.Reset();

		// Make sure GC referencer object is checked for references to other objects even if it resides in permanent object pool
		if (FPlatformProperties::RequiresCookedData() && GUObjectArray.IsDisregardForGC(FGCObject::GGCObjectReferencer))
		{
			InitialObjects.Add(FGCObject::GGCObjectReferencer);
		}

		{
			const double StartTime = FPlatformTime::Seconds();
			MarkObjectsAsUnreachable(KeepFlags);
			const double ElapsedTime = FPlatformTime::Seconds() - StartTime;
			if (!Stats.bFoundGarbageRef)
			{
				GGCStats.MarkObjectsAsUnreachableTime = ElapsedTime;
			}
			UE_LOG(LogGarbage, Verbose, TEXT("%f ms for MarkObjectsAsUnreachable Phase (%d Objects To Serialize)"), ElapsedTime * 1000, InitialObjects.Num());
		}
	}

	void PerformReachabilityAnalysisPass(const EGCOptions Options)
	{
		FContextPoolScope Pool;
		FWorkerContext* Context = nullptr;
		const bool bIsSingleThreaded = !(Options & EGCOptions::Parallel);

		if (GReachabilityState.IsSuspended())
		{
			Context = GReachabilityState.GetContextArray()[0];
			Context->bDidWork = false;
			InitialObjects.Reset();
		}
		else
		{
			Context = Pool.AllocateFromPool();
			if (bIsSingleThreaded)
			{
				GReachabilityState.SetupWorkers(1);
				GReachabilityState.GetContextArray()[0] = Context;
			}
		}

		if (!Private::GReachableObjects.IsEmpty())
		{
			// Add objects marked with the GC barrier to the inital set of objects for the next iteration of incremental reachability
			Private::GReachableObjects.PopAllAndEmpty(InitialObjects);
			GGCStats.NumBarrierObjects += InitialObjects.Num();
			UE_LOG(LogGarbage, Verbose, TEXT("Adding %d object(s) marker by GC barrier to the list of objects to process"), InitialObjects.Num());
			ConditionallyAddBarrierReferencesToHistory(*Context);
		}
		else if (GReachabilityState.GetNumIterations() == 0 || (Stats.bFoundGarbageRef && !GReachabilityState.IsSuspended()))
		{
			Context->InitialNativeReferences = GetInitialReferences(Options);
		}

		if (!Private::GReachableClusters.IsEmpty())
		{
			// Process cluster roots that were marked as reachable by the GC barrier
			TArray<FUObjectItem*> KeepClusterRefs;
			Private::GReachableClusters.PopAllAndEmpty(KeepClusterRefs);
			for (FUObjectItem* ObjectItem : KeepClusterRefs)
			{
				// Mark referenced clusters and mutable objects as reachable
				MarkReferencedClustersAsReachable<EGCOptions::None>(ObjectItem->GetClusterIndex(), InitialObjects);
			}
		}

		Context->SetInitialObjectsUnpadded(InitialObjects);

		PerformReachabilityAnalysisOnObjects(Context, Options);

		if (!GReachabilityState.CheckIfAnyContextIsSuspended())
		{
			GReachabilityState.ResetWorkers();
			Stats.AddStats(Context->Stats);
			GReachabilityState.UpdateStats(Context->Stats);
			Pool.ReturnToPool(Context);
		}
		else if (bIsSingleThreaded)
		{
			Context->ResetInitialObjects();
			Context->InitialNativeReferences = TConstArrayView<UObject**>();
		}
	}

public:

	/**
	 * Performs reachability analysis.
	 *
	 * @param KeepFlags		Objects with these flags will be kept regardless of being referenced or not
	 */
	void PerformReachabilityAnalysis(EObjectFlags KeepFlags, const EGCOptions Options)
	{
		LLM_SCOPE(ELLMTag::GC);

		const bool bIsGarbageTracking = !GReachabilityState.IsSuspended() && Stats.bFoundGarbageRef;

		if (!GReachabilityState.IsSuspended())
		{
			StartReachabilityAnalysis(KeepFlags, Options);
			// We start verse GC here so that the objects are unmarked prior to verse marking them
			StartVerseGC();
		}

		{
			const double StartTime = FPlatformTime::Seconds();

			do
			{
				PerformReachabilityAnalysisPass(Options);
			// NOTE: It is critical that VerseGCActive is called prior to checking GReachableObjects.  While VerseGCActive is true,
			// items can still be added to GReachableObjects.  So if reversed, during the point where GReachableObjects is checked
			// and VerseGCActive returns false, something might have been marked.  Reversing insures that Verse will not add anything 
			// if Verse is no longer active.
			} while ((VerseGCActive() || !Private::GReachableObjects.IsEmpty() || !Private::GReachableClusters.IsEmpty()) && !GReachabilityState.IsSuspended());

			const double ElapsedTime = FPlatformTime::Seconds() - StartTime;
			if (!bIsGarbageTracking)
			{
				GGCStats.ReferenceCollectionTime += ElapsedTime;
			}
			UE_LOG(LogGarbage, Verbose, TEXT("%f ms for Reachability Analysis"), ElapsedTime * 1000);
		}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		// Allowing external systems to add object roots. This can't be done through AddReferencedObjects
		// because it may require tracing objects (via FGarbageCollectionTracer) multiple times
		if (!GReachabilityState.IsSuspended())
		{
			FCoreUObjectDelegates::TraceExternalRootsForReachabilityAnalysis.Broadcast(*this, KeepFlags, !(Options & EGCOptions::Parallel));
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	bool VerseGCActive()
	{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		if (!GIsFrankenGCCollecting)
		{
			return false;
		}

		bool bIsDone = false;
		Verse::FIOContext::Create(
			[&bIsDone](Verse::FIOContext VerseContext) {
				bIsDone = Verse::FHeap::IsGCTerminationPendingExternalSignal(VerseContext);
			}
		);
		return !bIsDone;
#else
		return false;
#endif
	}

	virtual void PerformReachabilityAnalysisOnObjects(FWorkerContext* Context, EGCOptions Options) override
	{
		(this->*ReachabilityAnalysisFunctions[GetGCFunctionIndex(Options)])(*Context);
	}

	FProcessorStats Stats;
};