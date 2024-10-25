/**
 * This nested class is used to provide a UObject interface between non
 * UObject classes and the UObject system. It handles forwarding all
 * calls of AddReferencedObjects() to objects/ classes that register with it.
 * 这个嵌套类用于在非UObject类和UObjec系统之间提供一个接口。它处理将AddReferencedObjects（）的所有调用转发给注册到它的对象/类。
 */
class UGCObjectReferencer : public UObject
{
	struct FImpl;
	TUniquePtr<FImpl> Impl;

	/** Current FGCObject* that references are being added from  */
	FGCObject* CurrentlySerializingObject = nullptr;

	friend struct FReplaceReferenceHelper;

public:
	DECLARE_CASTED_CLASS_INTRINSIC_WITH_API_NO_CTOR(UGCObjectReferencer, UObject, CLASS_Transient, TEXT("/Script/CoreUObject"), CASTCLASS_None, NO_API);

	COREUOBJECT_API UGCObjectReferencer(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	COREUOBJECT_API UGCObjectReferencer(FVTableHelper& Helper);
	COREUOBJECT_API ~UGCObjectReferencer();

	/**
	 * Adds an object to the referencer list
	 *
	 * @param Object The object to add to the list
	 */
	COREUOBJECT_API void AddObject(FGCObject* Object);

	/**
	 * Removes an object from the referencer list
	 *
	 * @param Object The object to remove from the list
	 */
	COREUOBJECT_API void RemoveObject(FGCObject* Object);

	/**
	 * Get the name of the first FGCObject that owns this object.
	 *
	 * @param Object The object that we're looking for.
	 * @param OutName the name of the FGCObject that reports this object.
	 * @param bOnlyIfAddingReferenced Only try to find the name if we are currently inside AddReferencedObjects
	 * @return true if the object was found.
	 */
	COREUOBJECT_API bool GetReferencerName(UObject* Object, FString& OutName, bool bOnlyIfAddingReferenced = false) const;

	/**
	 * Forwards this call to all registered objects so they can reference
	 * any UObjects they depend upon
	 *
	 * @param InThis This UGCObjectReferencer object.
	 * @param Collector The collector of referenced objects.
	 */
	COREUOBJECT_API static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	void AddInitialReferences(TArray<UObject**>& Out);
	/**
	 * Destroy function that gets called before the object is freed. This might
	 * be as late as from the destructor.
	 */
	virtual void FinishDestroy() override;

	/**
	 * Returns the currently serializing object
	 */
	FGCObject* GetCurrentlySerializingObject() const
	{
		return CurrentlySerializingObject;
	}

#if WITH_EDITORONLY_DATA
	/** Called when a new FGCObject is added to this referencer */
	DECLARE_MULTICAST_DELEGATE_OneParam(FGCObjectAddedDelegate, FGCObject*);

private:

	/** Called when a new FGCObject is added to this referencer */
	FGCObjectAddedDelegate OnGCObjectAdded;

public:

	/** Returns a delegate called when a new FGCObject is added to this referencer */
	FGCObjectAddedDelegate& GetGCObjectAddedDelegate()
	{
		return OnGCObjectAdded;
	}
#endif // WITH_EDITORONLY_DATA
};
