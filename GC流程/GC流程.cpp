void CollectGarbage(EObjectFlags KeepFlags, bool bPerformFullPurge)
{
	UE::GC::CollectGarbageInternal(KeepFlags, bPerformFullPurge);
	{
		//void FReachabilityAnalysisState::CollectGarbage(EObjectFlags KeepFlags, bool bFullPurge)
		GReachabilityState.CollectGarbage(KeepFlags, bPerformFullPurge);
        {
			if (GIsIncrementalReachabilityPending)
			{
				// Something triggered a new GC run but we're in the middle of incremental reachability analysis.
				// Finish the current GC pass (including purging all unreachable objects) and then kick off another GC run as requested
				// 某些东西触发了新的GC运行，但我们正在进行增量可达性分析。
				// 完成当前的GC传递（包括清除所有不可访问的对象），然后根据请求启动另一个GC运行
				bPerformFullPurge = true;
				PerformReachabilityAnalysisAndConditionallyPurgeGarbage(/*bReachabilityUsingTimeLimit =*/ false);

				checkf(!GIsIncrementalReachabilityPending, TEXT("Flushing incremental reachability analysis did not complete properly"));

				// Need to acquire GC lock again as it was released in PerformReachabilityAnalysisAndConditionallyPurgeGarbage() -> UE::GC::PostCollectGarbageImpl()
				// 需要再次获取GC锁，因为它在PerformReachabilityAnalysisAndConditionallyPurgeGarbage（）中被释放-> UE::GC::PostCollectGarbageImpl（）
				AcquireGCLock();
			}

			...
			const bool bReachabilityUsingTimeLimit = !bFullPurge && GAllowIncrementalReachability;

			/** Performs Reachability Analysis (also incrementally) and destroys objects (also incrementally) */
			//执行可达性分析（也是增量的）并销毁对象（也是增量的）
           	//void FReachabilityAnalysisState::PerformReachabilityAnalysisAndConditionallyPurgeGarbage(bool bReachabilityUsingTimeLimit)
			PerformReachabilityAnalysisAndConditionallyPurgeGarbage(bReachabilityUsingTimeLimit)	
			{
				/** 
				 * Deletes all unreferenced objects, keeping objects that have any of the passed in KeepFlags set
				 //删除所有未引用的对象，保留在KeepFlags中传递的对象
				 *
				 * @param	KeepFlags			objects with those flags will be kept regardless of being referenced or not
				 * @param	bPerformFullPurge	if true, perform a full purge after the mark pass
				 */
				UE::GC::PreCollectGarbageImpl<true/false>(ObjectKeepFlags);  //bPerformFullPurge 
				{
					// We can't collect garbage while there's a load in progress. E.g. one potential issue is Import.XObject
					//我们不能在装载过程中收集垃圾。例如，一个潜在的问题是导入某对象
					// Reset GC skip counter
					//重置GC跳过计数器
					// Flush streaming before GC if requested
					//如果请求，在GC之前刷新流
					
					
					// Route callbacks so we can ensure that we are e.g. not in the middle of loading something by flushing
					// the async loading, etc...
					//路由(广播)回调，这样我们就可以确保我们不会通过刷新异步加载来加载某些东西，等等…
					if (!GIsIncrementalReachabilityPending)
					{
						FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Broadcast();
					}
					
					
					// Make sure previous incremental purge has finished or we do a full purge pass in case we haven't kicked one
					// off yet since the last call to garbage collection.	
					//确保之前的增量清除已经完成，或者在上次调用垃圾收集之后还没有启动一次清除的情况下，执行一次完整的清除。
					if (IsIncrementalPurgePending())
					{	
						/**
						 * Incrementally purge garbage by deleting all unreferenced objects after routing Destroy.
						 *在发送Destroy之后，通过删除所有未引用的对象来增量清除垃圾。
						 *
						 * Calling code needs to be EXTREMELY careful when and how to call this function as 
						 * RF_Unreachable cannot change on any objects unless any pending purge has completed!
						 * 调用代码需要非常小心何时以及如何调用这个函数，因为RF_Unreachable不能在任何对象上更改，除非任何挂起清除已经完成！

						 * @param	bUseTimeLimit	whether the time limit parameter should be used
						 * @param	TimeLimit		soft time limit for this function call  //默认值 0.002
						 */
						IncrementalPurgeGarbage(false);
						{
							// Early out if there is nothing to do
							// 如果没事可做，就早点出去
							if (!GObjPurgeIsRequired && !GObjIncrementalPurgeIsInProgress)
							{
								return;
							}
							if (!GObjPurgeIsRequired)
							{
								FMemory::Trim();
								bCompleted = true;
							}
							else
							{
								// Set 'I'm garbage collecting' flag - might be checked inside various functions.
								//设置“我正在垃圾收集”标志-可以在各种函数中检查。
								TGuardValue<bool> GuardIsGarbageCollecting(GIsGarbageCollecting, true);


								// Keep track of start time to enforce time limit unless bForceFullPurge is true;
								// 跟踪开始时间以强制执行时间限制，除非bForceFullPurge为true；
								GCStartTime = FPlatformTime::Seconds();
								bool bTimeLimitReached = false;

								//COREUOBJECT_API bool IsIncrementalUnhashPending();
								/**
								* Checks if there's objects pending to be unhashed when running incremental purge
								* 检查在运行增量清除时是否有待解散的对象
								* @return true if the time limit passed and there's still objects pending to be unhashed
								如果超过时间限制，并且仍然有待解列的对象，则返回true
								*/
								if (IsIncrementalUnhashPending())
								{

									
									//bool UnhashUnreachableObjects(bool bUseTimeLimit, double TimeLimit)
									/**
									* Calls ConditionalBeginDestroy on unreachable objects
									对不可达对象调用 ConditionalBeginDestroy
									*
									* @param	bUseTimeLimit	whether the time limit parameter should be used
									* @param	TimeLimit		soft time limit for this function call
									*
									* @return true if the time limit passed and there's still objects pending to be unhashed
									*/
									bTimeLimitReached = UnhashUnreachableObjects(bUseTimeLimit, TimeLimit);
									//bool UnhashUnreachableObjects(bool bUseTimeLimit, double TimeLimit)
									{
										...
										if (GGatherUnreachableObjectsState.IsPending())
										{
											// Incremental Gather needs to be called from UnhashUnreachableObjects to match changes in IsIncrementalUnhashPending() (and not introduce IsIncrementalGatherPending())
											// 增量收集需要从UnhashUnreachableObjects中调用，以匹配IsIncrementalUnhashPending(）中的变化（而不是引入IsIncrementalGatherPending()）。
											const EGatherOptions GatherOptions = GetObjectGatherOptions();
											const double GatherTimeLimit = GIncrementalGatherTimeLimit > 0.0f ? GIncrementalGatherTimeLimit : TimeLimit;
											bTimeLimitReached = GatherUnreachableObjects(GatherOptions, bUseTimeLimit ? GatherTimeLimit : 0.0);
											if (!bTimeLimitReached)
											{
												if (bUseTimeLimit)
												{
													TimeLimit -= FMath::Min(TimeLimit, FPlatformTime::Seconds() - GCStartTime);
												}
											}
											else
											{
												return bTimeLimitReached;
											}
										}	
											...
										// Unhash all unreachable objects.
										//解绑定所有不可达对象。
										while (GUnrechableObjectIndex < GUnreachableObjects.Num())
										{
											FUObjectItem* ObjectItem = GUnreachableObjects[GUnrechableObjectIndex++];
											{
												UObject* Object = static_cast<UObject*>(ObjectItem->Object);
												FScopedCBDProfile Profile(Object);
												// Begin the object's asynchronous destruction.
												// 开始对象的异步销毁
												Object->ConditionalBeginDestroy();
												/**
												 * Called before destroying the object.  This is called immediately upon deciding to destroy the object, to allow the object to begin an
												 * asynchronous cleanup process.
												 * 在销毁对象之前调用。在决定销毁对象时立即调用该函数，以允许对象开始异步清理过程。
												 */
												//bool UObject::ConditionalBeginDestroy()
												{
													...						
													//	COREUOBJECT_API virtual void BeginDestroy();			
													BeginDestroy();
													/**
													 * Called before destroying the object.  This is called immediately upon deciding to destroy the object, to allow the object to begin an
													 * asynchronous cleanup process.
													 * 在销毁对象之前调用。
													在决定销毁对象时立即调用该函数，以允许对象开始异步清理过程。
													 */
													{
														// Sanity assertion to ensure ConditionalBeginDestroy is the only code calling us.
														// 完整性断言以确保ConditionalBeginDestroy是唯一调用我们的代码。

															// Remove from linker's export table.
															SetLinker( NULL, INDEX_NONE );
															// 	COREUOBJECT_API void SetLinker( FLinkerLoad* LinkerLoad, int32 LinkerIndex, bool bShouldDetachExisting=true )
															/**
															 * Changes the linker and linker index to the passed in one. A linker of NULL and linker index of INDEX_NONE
															 * indicates that the object is without a linker.
															 * 将链接器和链接器索引更改为传递的链接器索引。链接器为NULL且链接器索引为INDEX_NONE表示该对象没有链接器。

															 * @param LinkerLoad				New LinkerLoad object to set
															 * @param LinkerIndex				New LinkerIndex to set
															 * @param bShouldDetachExisting		If true, detach existing linker and call PostLinkerChange
															 */	
															{
																...//SetLinker 内容
															}
															
															//	COREUOBJECT_API void LowLevelRename(FName NewName,UObject *NewOuter = NULL);
															/**
															 * Just change the FName and Outer and rehash into name hash tables. For use by higher level rename functions.
															 * 只需更改FName和Outer并将其重新散列到名称散列表中。供高级重命名函数使用。
															 * @param NewName	new name for this object
															 * @param NewOuter	new outer for this object, if NULL, outer will be unchanged
															 */
															LowLevelRename(NAME_None);
															{
																...//LowLevelRename
															}
															// Remove any associated external package, at this point
															// 此时，删除任何相关的外部包
															SetExternalPackage(nullptr);

															// Destroy any associated property bag.
															// 销毁任何相关资产包
															UE::FPropertyBagRepository::Get().DestroyOuterBag(this);

													}
													...
												}
											}
											
										}
										...
										return bTimeLimitReached;
									}

									if (GUnrechableObjectIndex >= GUnreachableObjects.Num())
									{
										FScopedCBDProfile::DumpProfile();
									}
								}
								
								if (!bTimeLimitReached)
								{
									bCompleted = IncrementalDestroyGarbage(bUseTimeLimit, TimeLimit);
									{
										// Keep track of time it took to destroy objects for stats
										// 记录销毁对象的时间
										const double IncrementalDestroyGarbageStartTime = FPlatformTime::Seconds();
										double LastTimeoutWarningTime = IncrementalDestroyGarbageStartTime;	

										// Depending on platform FPlatformTime::Seconds might take a noticeable amount of time if called thousands of times so we avoid
										// enforcing the time limit too often, especially as neither Destroy nor actual deletion should take significant
										// amounts of time.
										// 根据不同的平台，如果多次调用FPlatformTime::Seconds可能会花费大量的时间，所以我们避免过于频繁地强制执行时间限制，特别是在销毁和实际删除都不需要花费大量时间的情况下。
										const int32	TimeLimitEnforcementGranularityForDestroy = 10;
										const int32	TimeLimitEnforcementGranularityForDeletion = 100;

										// Set 'I'm garbage collecting' flag - might be checked inside UObject::Destroy etc.
										// 设置“我正在垃圾收集”标志-可能在UObject::Destroy等中检查。
										TGuardValue<bool> GuardIsGarbageCollecting(GIsGarbageCollecting, true);

										if( !GObjFinishDestroyHasBeenRoutedToAllObjects && !bTimeLimitReached )
										{
											check(GUnrechableObjectIndex >= GUnreachableObjects.Num());
											// Try to dispatch all FinishDestroy messages to unreachable objects.  We'll iterate over every
											// single object and destroy any that are ready to be destroyed.  The objects that aren't yet
											// ready will be added to a list to be processed afterwards.
											// 尝试将所有FinishDestroy消息分派给不可访问的对象。
											// 我们将遍历每个对象，并销毁任何准备销毁的对象。
											// 尚未准备好的对象将被添加到一个列表中，稍后再进行处理。
											int32 TimeLimitTimePollCounter = 0;
											int32 FinishDestroyTimePollCounter = 0;
										}
										{
											while (GObjCurrentPurgeObjectIndex < GUnreachableObjects.Num())
											{
												FUObjectItem* ObjectItem = GUnreachableObjects[GObjCurrentPurgeObjectIndex];
												if (ObjectItem->IsUnreachable())
												{
													UObject* Object = static_cast<UObject*>(ObjectItem->Object);
													// Object should always have had BeginDestroy called on it and never already be destroyed
													// 对象应该总是调用了BeginDestroy，并且永远不要已经被销毁
													check( Object->HasAnyFlags( RF_BeginDestroyed ) && !Object->HasAnyFlags( RF_FinishDestroyed ) );

													// Only proceed with destroying the object if the asynchronous cleanup started by BeginDestroy has finished.
													// 只有在BeginDestroy开始的异步清理完成后才继续销毁对象。
													if(Object->IsReadyForFinishDestroy())
													{
														UE::GC::GDetailedStats.IncPurgeCount(Object);
														// Send FinishDestroy message.
														// 发送FinishDestroy消息。
														Object->ConditionalFinishDestroy();
														//bool UObject::ConditionalFinishDestroy()
														{
															check(IsValidLowLevel());
															if( !HasAnyFlags(RF_FinishDestroyed) )
															{
																SetFlags(RF_FinishDestroyed);
																#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
																	checkSlow(!DebugFinishDestroyed.Contains(this));
																	DebugFinishDestroyed.Add(this);
																#endif
																FinishDestroy();
																//void UObject::FinishDestroy()
																{
																	if( !HasAnyFlags(RF_FinishDestroyed) )
																	{
																		UE_LOG(LogObj, Fatal,
																			TEXT("Trying to call UObject::FinishDestroy from outside of UObject::ConditionalFinishDestroy on object %s. Please fix up the calling code."),
																			*GetName()
																			);
																	}

																	check( !GetLinker() );
																	check( GetLinkerIndex()	== INDEX_NONE );

																	DestroyNonNativeProperties();

																	#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
																		DebugFinishDestroyed.RemoveSingle(this);
																	#endif
																}

																// Make sure this object can't be accessed via weak pointers after it's been FinishDestroyed
																// 确保这个对象在被finishdestroy后不能通过弱指针访问
																GUObjectArray.ResetSerialNumber(this);

																// Make sure this object can't be found through any delete listeners (annotation maps etc) after it's been FinishDestroyed
																//确保这个对象在被finishdestroy后不会被任何删除监听器（annotation maps等）找到
																GUObjectArray.RemoveObjectFromDeleteListeners(this);

																#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
																		if( DebugFinishDestroyed.Contains(this) )
																		{
																			UE_LOG(LogObj, Fatal, TEXT("%s failed to route FinishDestroy"), *GetFullName() );
																		}
																#endif
																return true;
															}
															else 
															{
																return false;
															}


														}
													}
													else
													{
														// The object isn't ready for FinishDestroy to be called yet.  This is common in the
														// case of a graphics resource that is waiting for the render thread "release fence"
														// to complete.  Just calling IsReadyForFinishDestroy may begin the process of releasing
														// a resource, so we don't want to block iteration while waiting on the render thread.
														// 对象还没有准备好调用FinishDestroy。
														// 这在等待渲染线程“释放栅栏”完成的图形资源的情况下很常见。
														// 只是调用IsReadyForFinishDestroy可能会开始释放资源的过程，所以我们不想在等待渲染线程时阻塞迭代。

														// Add the object index to our list of objects to revisit after we process everything else
														GGCObjectsPendingDestruction.Add(Object);
														GGCObjectsPendingDestructionCount++;
													}
												}
												
											}
										}
									}
								}
								if (bCompleted)
								{
									// Broadcast the post-purge garbage delegate to give systems a chance to clean up things
									// that might have been referenced by purged objects.
									////广播清除后的垃圾委托，让系统有机会清理可能被清除对象引用的东西。
									TRACE_CPUPROFILER_EVENT_SCOPE(BroadcastPostPurgeGarbage);
									FCoreUObjectDelegates::GetPostPurgeGarbageDelegate().Broadcast();
								}
							}
						}
					}
					
					
					// The hash tables are only locked during this scope of reachability analysis.
					// 哈希表仅在可达性分析范围内被锁定。
					/**
					 * Prevents any other threads from finding/adding UObjects (e.g. while GC is running)
					 防止任何其他线程查找/添加UObjects（例如，当GC正在运行时）
					*/
					LockUObjectHashTables();
				}
				
				//void FReachabilityAnalysisState::PerformReachabilityAnalysis()
				PerformReachabilityAnalysis()
				{
					...
					if (bPerformFullPurge)
					{
						UE::GC::CollectGarbageFull(ObjectKeepFlags);
						//FORCENOINLINE static void CollectGarbageFull(EObjectFlags KeepFlags)
						{
							//template<bool bPerformFullPurge>
							// void CollectGarbageImpl(EObjectFlags KeepFlags)
							// FORCENOINLINE void CollectGarbageImpl(EObjectFlags KeepFlags);
							CollectGarbageImpl<true>(KeepFlags);
							{
								const EGCOptions Options = GetReferenceCollectorOptions(bPerformFullPurge);
								//FRealtimeGC 
								//void PerformReachabilityAnalysis(EObjectFlags KeepFlags, const EGCOptions Options)
								GC.PerformReachabilityAnalysis(KeepFlags, Options);
								{
									if (!GReachabilityState.IsSuspended())
									{
										StartReachabilityAnalysis(KeepFlags, Options);
										{
											BeginInitialReferenceCollection(Options);
											{
												InitialReferences.Reset();

												if (IsParallel(Options))
												{
													InitialCollection = UE::Tasks::Launch(TEXT("CollectInitialReferences"), 
														[&] () { FGCObject::GGCObjectReferencer->AddInitialReferences(InitialReferences); });
											
													// void UGCObjectReferencer::AddInitialReferences(TArray<UObject**>& Out)
													{
														FInitialReferenceCollector Collector(Out);
														for (FGCObject* Object : Impl->InitialReferencedObjects)
														{
															Object->AddReferencedObjects(Collector);
														}
													}
												}
											}
											// Reset object count.
											GObjectCountDuringLastMarkPhase.Reset();

											InitialObjects.Reset();

											// Make sure GC referencer object is checked for references to other objects even if it resides in permanent object pool
											//确保检查GC引用对象对其他对象的引用，即使它驻留在永久对象池中
											if (FPlatformProperties::RequiresCookedData() && GUObjectArray.IsDisregardForGC(FGCObject::GGCObjectReferencer))
											{
												InitialObjects.Add(FGCObject::GGCObjectReferencer);
											}
												
											/**
											* Marks all objects that don't have KeepFlags and EInternalObjectFlags_GarbageCollectionKeepFlags as MaybeUnreachable
											* 标记所有没有KeepFlags和elinternalobjectflags_garbagecollectionkeepflags的对象为MaybeUnreachable
											*/
											MarkObjectsAsUnreachable(KeepFlags);
											{
												// Don't swap the flags if we're re-entering this function to track garbage references
												// 如果我们重新进入这个函数来跟踪垃圾引用，不要交换标志
												if (const bool bInitialMark = !Stats.bFoundGarbageRef)
												{
													// This marks all UObjects as MaybeUnreachable
													// 这将所有uobject标记为MaybeUnreachable
													Swap(GReachableObjectFlag, GMaybeUnreachableObjectFlag);
												}

												// Not counting the disregard for GC set to preserve legacy behavior
												//不包括忽略GC集以保留遗留行为
												GObjectCountDuringLastMarkPhase.Set(GUObjectArray.GetObjectArrayNumMinusAvailable() - GUObjectArray.GetFirstGCIndex());


												// Now make sure all clustered objects and root objects are marked as Reachable. 
												//现在确保所有集群对象和根对象都被标记为可达。
												// This could be considered as initial part of reachability analysis and could be made incremental.
												//这可以被视为可达性分析的初始部分，并且可以逐步进行。
												MarkClusteredObjectsAsReachable(GatherOptions, InitialObjects);
												{
													// StartGathering calculates the number of threads based on the number of objects but here the objects are actually clusters
													// that contain many more objects than the number of clusters so we want to be able to process at least two clusters per thread	
													// StartGathering根据对象的数量计算线程的数量，但这里的对象实际上是包含比集群数量更多的对象的集群，所以我们希望每个线程能够处理至少两个集群
													const int32 NumThreads = !!(Options & EGatherOptions::Parallel) ? FMath::Min(GetNumCollectReferenceWorkers(), (ClusterArray.Num() + 1) / 2) : 1;
													GatherClustersState.Start(Options, ClusterArray.Num(), /* FirstIndex = */ 0, NumThreads);
													FMarkClustersState::FThreadIterators& ThreadIterators = GatherClustersState.GetThreadIterators();


													for (FUObjectItem* ObjectItem : MarkClustersResults.ClustersToDissolve)
													{
														// Check if the object is still a cluster root - it's possible one of the previous
														// DissolveClusterAndMarkObjectsAsUnreachable calls already dissolved its cluster
														//检查对象是否仍然是一个集群根—可能是 前面的DissolveClusterAndMarkObjectsAsUnreachable调用 已经解散了它的集群
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
														// 这个东西肯定没有被标记为不可达，所以不要在这里测试。确保所有引用的集群也被标记为可达
														MarkReferencedClustersAsReachable<EGCOptions::None>(ObjectItem->GetClusterIndex(), OutRootObjects);
													}
												}
												
												MarkRootObjectsAsReachable(GatherOptions, KeepFlags, InitialObjects);	
												{
													ParallelFor
													{
														while (ThreadState.Index <= ThreadState.LastIndex)
														{
															FUObjectItem* RootItem = &GUObjectArray.GetObjectItemArrayUnsafe()[RootsArray[ThreadState.Index++]];
															UObject* Object = static_cast<UObject*>(RootItem->Object);
															// IsValidLowLevel is extremely slow in this loop so only do it in debug
															checkSlow(Object->IsValidLowLevel());			
															
															
															//FORCEINLINE void FastMarkAsReachableInterlocked_ForGC()
															//Mark this object item as Reachable and clear MaybeUnreachable flag. For GC use only.
															//将此对象项标记为可达，并清除MaybeUnreachable标志。仅供GC使用。
															RootItem->FastMarkAsReachableInterlocked_ForGC();
															
															ThreadState.Payload.Add(Object);
														}
													}


													// This is super slow as we need to look through all existing UObjects and access their memory to check EObjectFlags
													// 这是超级慢的，因为我们需要查看所有现有的UObjects并访问它们的内存来检查EObjectFlags
													if (KeepFlags != RF_NoFlags)
													{
														ParallelFor
														{
															while (ThreadState.Index <= ThreadState.LastIndex)
															{
																FUObjectItem* RootItem = &GUObjectArray.GetObjectItemArrayUnsafe()[RootsArray[ThreadState.Index++]];
																UObject* Object = static_cast<UObject*>(RootItem->Object);

																// IsValidLowLevel is extremely slow in this loop so only do it in debug
																checkSlow(Object->IsValidLowLevel());					

																RootItem->FastMarkAsReachableInterlocked_ForGC();
																ThreadState.Payload.Add(Object);
															}
														}
													}

													// Preallocate the resulting array taking both MarkRootsState and MarkObjectsState results into account to avoild reallocating OutRootObjects in each of the Finish() calls.
													// 预先分配结果数组，同时考虑MarkRootsState和MarkObjectsState结果，以避免在每次Finish（）调用中重新分配outrootobject。
													...
												}
											}

										}	
									}
								}
							}
						}							

					}
					else if (NumRechabilityIterationsToSkip == 0 || // Delay reachability analysis by NumRechabilityIterationsToSkip (if desired)
							!bIsSuspended || // but only but only after the first iteration (which also does MarkObjectsAsUnreachable)
							IterationTimeLimit <= 0.0f) // and only when using time limit (we're not using the limit when we're flushing reachability analysis when starting a new one or on exit)
					{
						UE::GC::CollectGarbageIncremental(ObjectKeepFlags);
					}
					else
					{
						--NumRechabilityIterationsToSkip;
					}

				}

				UE::GC::PostCollectGarbageImpl<true/false>(ObjectKeepFlags);  //bPerformFullPurge true/false

			}
		}
	}
}