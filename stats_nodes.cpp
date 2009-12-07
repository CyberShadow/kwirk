// Node statistics

#ifdef STATS_NODES
NODEI newNodes, duplicateNodes, reparents;
#define STATS_NODES_DO(x) x

void printNodeStats()
{
	printf("%d new nodes, %d duplicate nodes, %d reparents\n", newNodes, duplicateNodes, reparents);
}

#else
#define STATS_NODES_DO(x)
#endif
