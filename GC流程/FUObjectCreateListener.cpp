	/**
	 * Base class for UObjectBase create class listeners
	 */
	class FUObjectCreateListener
	{
	public:
		virtual ~FUObjectCreateListener() {}
		/**
		* Provides notification that a UObjectBase has been added to the uobject array
		 *提供UObjectBase已添加到uobject数组的通知
		 * @param Object object that has been destroyed
		 * @param Index	index of object that is being deleted
		 */
		virtual void NotifyUObjectCreated(const class UObjectBase *Object, int32 Index)=0;

		/**
		 * Called when UObject Array is being shut down, this is where all listeners should be removed from it 
         * 当UObject Array被关闭时调用，这是所有侦听器应该从中移除的地方
		 */
		virtual void OnUObjectArrayShutdown()=0;
	};