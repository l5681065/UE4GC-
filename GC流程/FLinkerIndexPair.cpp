// Check that the mapping of UObjects to linkers is sparse and that we aren't spending a ton of time with these lookups.
struct FLinkerIndexPair
{
	/**
	 * default contructor
	 * Default constructor must be the default item
	 */
	FLinkerIndexPair() :
		Linker(NULL),
		LinkerIndex(INDEX_NONE)
	{
		CheckInvariants();
	}
	/**
	 * Determine if this linker pair is the default
	 * @return true is this is a default pair. We only check the linker because CheckInvariants rules out bogus combinations
	 */
	FORCEINLINE bool IsDefault()
	{
		CheckInvariants();
		return Linker == nullptr;
	}

	/**
	 * Constructor 
	 * @param InLinker linker to assign
	 * @param InLinkerIndex linker index to assign
	 */
	FLinkerIndexPair(FLinkerLoad* InLinker, int32 InLinkerIndex) :
		Linker(InLinker),
		LinkerIndex(InLinkerIndex)
	{
		CheckInvariants();
	}

	/**
	 * check() that either the linker and the index is valid or neither of them are
	 */
	FORCEINLINE void CheckInvariants()
	{
		check(!((Linker == nullptr) ^ (LinkerIndex == INDEX_NONE))); // you need either a valid linker and index or neither valid
	}

	/**
	 * Linker that contains the FObjectExport resource corresponding to
	 * this object.  NULL if this object is native only (i.e. never stored
	 * in an Unreal package), or if this object has been detached from its
	 * linker, for e.g. renaming operations, saving the package, etc.
	 *  包含与此对象对应的FObjectExport资源的链接器。
        如果此对象仅为本机（即从未存储在虚幻包中），或者如果此对象已从其链接器中分离，例如重命名操作，保存包等，则为NULL。
    */
	FLinkerLoad*			Linker; 

	/**
	 * Index into the linker's ExportMap array for the FObjectExport resource
	 * corresponding to this object.
	 */
	int32							LinkerIndex; 
};