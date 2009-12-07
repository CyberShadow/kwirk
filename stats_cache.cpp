// Cache statistics

int cacheTrims, lastCacheTrims;
unsigned long cacheMisses, cacheWrites;
unsigned long lastCacheMisses, lastCacheWrites;

INLINE void onCacheTrim() { cacheTrims++; }
INLINE void onCacheMiss() { cacheMisses++; }
INLINE void onCacheWrite() { cacheWrites++; }

#ifdef STATS_CACHE

unsigned long cacheHits;
unsigned long lastCacheHits;

INLINE void onCacheHit() { cacheHits++; }

#else

INLINE void onCacheHit() {}

#endif

void printCacheStats()
{
#ifdef STATS_CACHE
	printf("Cache totals: %lu trims, %lu writes, %lu hits, %lu misses\n", cacheTrims, cacheWrites, cacheHits, cacheMisses);
#else
	printf("Cache totals: %lu trims, %lu writes, %lu misses\n", cacheTrims, cacheWrites, cacheMisses);
#endif
}

void printCacheStatsDelta()
{
	printf("\t%lu trims, %lu writes, "
#ifdef STATS_CACHE
		"%lu hits, "
#endif
		"%lu misses | %d/%d (%d%%)\n", 
		cacheTrims-lastCacheTrims, cacheWrites-lastCacheWrites, 
#ifdef STATS_CACHE
		cacheHits-lastCacheHits, 
#endif
		cacheMisses-lastCacheMisses,
		cacheSize, CACHE_SIZE, cacheSize*100LL/CACHE_SIZE);
	lastCacheTrims = cacheTrims;
	lastCacheWrites = cacheWrites;
#ifdef STATS_CACHE
	lastCacheHits = cacheHits;
#endif
	lastCacheMisses = cacheMisses;
}

