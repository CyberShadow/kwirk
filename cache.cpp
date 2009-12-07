#ifdef MULTITHREADING
MUTEX cacheMutex;
#endif

#if defined(CACHE_SPLAY)
#include "cache_splay.cpp"
#elif defined(CACHE_HASH)
#include "cache_hash.cpp"
#else
#error Cache plugin not set
#endif

#define CACHE_TRIM_THRESHOLD (CACHE_SIZE-(X*Y*2*THREADS))

#ifdef MULTITHREADING
int threadsReady = 0;
CONDITION trimReady, trimDone;
#endif

void postNode()
{
	if (cacheSize >= CACHE_TRIM_THRESHOLD)
	{
#ifdef MULTITHREADING
		/* LOCK */
		{
			SCOPED_LOCK lock(cacheMutex);

			if (cacheSize >= CACHE_TRIM_THRESHOLD) // synchronized check
			{
				if (threadsReady==0)
				{
					threadsReady = 1;
					CONDITION_RESET(trimReady);
					CONDITION_RESET(trimDone);
					while (threadsReady < threadsRunning)
						CONDITION_WAIT(trimReady, lock);
					cacheTrim();
					assert(cacheSize < CACHE_TRIM_THRESHOLD, "Trim failed");
					threadsReady = 0;
					CONDITION_NOTIFY(trimDone, lock);
				}
				else
				{
					threadsReady++;
					CONDITION_NOTIFY(trimReady, lock);
					CONDITION_WAIT(trimDone, lock);
					assert(cacheSize < CACHE_TRIM_THRESHOLD, "Trim failed");
				}
			}

		}
		CONDITION_BARRIER(trimDone);
#else
		cacheTrim();
		assert(cacheSize < CACHE_TRIM_THRESHOLD, "Trim failed");
#endif
	}
}

void onThreadExit()
{
#ifdef MULTITHREADING
	/* LOCK */
	{
		SCOPED_LOCK lock(cacheMutex);
		threadsRunning--;
		CONDITION_NOTIFY(trimReady, lock);
	}
#endif
}

INLINE void markDirty(Node* np)
{
	((CacheNode*)np)->dirty = true;
}

#ifndef HAVE_CACHE_PEEK

#define PEEK_BUF_SIZE 102400
Node peekBuf[PEEK_BUF_SIZE];
NODEI peekPos = 0;
#ifdef MULTITHREADING
MUTEX peekMutex;
#endif

INLINE const Node* cachePeek(NODEI index)
{
	NODEI p;
	/* LOCK */
	{
#ifdef MULTITHREADING
		SCOPED_LOCK lock(peekMutex);
#endif
		p = peekPos++;
	}
	cacheUnarchive(index, &peekBuf[p]);
	return &peekBuf[p];
}

#endif // HAVE_CACHE_PEEK
