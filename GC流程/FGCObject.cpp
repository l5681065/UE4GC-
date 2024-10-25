/**
 * This class provides common registration for garbage collection for
 * non-UObject classes. It is an abstract base class requiring you to implement
 * the AddReferencedObjects() method.
 * 该类为非uobject类的垃圾收集提供通用注册。它是一个抽象基类，需要您实现AddReferencedObjects（）方法。
 */
class FGCObject
{
public:
	/**
	 * The static object referencer object that is shared across all
	 * garbage collectible non-UObject objects.
     * 在所有可垃圾收集的非uobject对象之间共享的静态对象引用器对象。
	 */
	static COREUOBJECT_API UGCObjectReferencer* GGCObjectReferencer;

	/** Initializes the global object referencer and adds it to the root set. */
	static COREUOBJECT_API void StaticInit();

	/**
	 * Tells the global object that forwards AddReferencedObjects calls on to objects
	 * that a new object is requiring AddReferencedObjects call.
	 */
	FGCObject()
	{
		RegisterGCObject();
	}

	FGCObject(const FGCObject& Other)
	{
		RegisterGCObject();
	}

	FGCObject(FGCObject&& Other)
	{
		RegisterGCObject();
	}

	enum class EFlags : uint32
	{
		None = 0,

		/** Manually call RegisterGCObject() later to avoid collecting references from empty / late-initialized FGCObjects */
		RegisterLater = 1 << 0,

		/**
		 * Declare that AddReferencedObjects *only* calls FReferenceCollector::AddStableReference*() functions
		 * instead of the older FReferenceCollector::AddReferenceObject*() and only adds native references.
		 * 
		 * Allows gathering initial references before reachability analysis starts.
		 */
		AddStableNativeReferencesOnly = 1 << 1,
	};
	COREUOBJECT_API explicit FGCObject(EFlags Flags);

	virtual ~FGCObject()
	{
		UnregisterGCObject();
	}

	FGCObject& operator=(const FGCObject&) {return *this;}
	FGCObject& operator=(FGCObject&&) {return *this;}

	/** Register with GC, only needed if constructed with EFlags::RegisterLater or after unregistering */
	COREUOBJECT_API void RegisterGCObject();

	/** Unregister ahead of destruction. Safe to call multiple times. */
	COREUOBJECT_API void UnregisterGCObject();

	/**
	 * Pure virtual that must be overloaded by the inheriting class. Use this
	 * method to serialize any UObjects contained that you wish to keep around.
	 *
	 * @param Collector The collector of referenced objects.
	 */
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) = 0;

	/** Overload this method to report a name for your referencer */
	virtual FString GetReferencerName() const = 0;

	/** Overload this method to report how the specified object is referenced, if necessary */
	virtual bool GetReferencerPropertyName(UObject* Object, FString& OutPropertyName) const
	{
		return false;
	}

private:
	friend UGCObjectReferencer;
	static COREUOBJECT_API const TCHAR* UnknownGCObjectName;

	const bool bCanMakeInitialReferences = false;
	bool bReferenceAdded = false;	
};