// "Look-forward" DFS with special loop handling.

#ifdef MULTITHREADING
#error Not currently supported
#endif

#define MAX_DEPTH    0x4000

#define DISTANCE_INF 0xFFFF
#define IN_STACK     0x8000
#define NO_LOOP      0x7FFF

int iteration=1, depth=0;

NODEI stackNodes[MAX_DEPTH];
State stackStates[MAX_DEPTH];

// ******************************************************************************************************

INLINE void replayStep(State* state, Step step) // faster version
{
	Player* p = &state->players[state->activePlayer];
	int nx = step.x+1;
	int ny = step.y+1;
	p->x = nx;
	p->y = ny;
	assert(state->map[ny][nx]==0, "Bad coordinates");
	int res = state->perform((Action)step.action);
	assert(res>0, "Replay failed");
}

#if 1
INLINE void replayState(Node* np, NODEI n, State* state)
{
	Step steps[MAX_DEPTH+1];
	unsigned int stepNr = 0;

	while (np->lastVisit < IN_STACK)
	{
		steps[stepNr++] = np->step;
		assert(stepNr <= MAX_DEPTH);
		
		n = np->parent;
		if (n==0)
		{
			if (getNode(1)->lastVisit >= IN_STACK) // if first node is in stack, then we missed it
				error("State chain does not start from stack");
            *state = initialState;
            goto play;
		}
		np = getNode(n);
	}
	
	/* scope for stackPos */
	{
		int stackPos = np->lastVisit - IN_STACK;
		assert(stackNodes[stackPos] == n, "Incorrect stackpos");
		*state = stackStates[stackPos];
	}
play:
	while (stepNr)
		replayStep(state, steps[--stepNr]);
}
#else
void replayState(Node* np, NODEI n, State* state)
{
	Step steps[MAX_STEPS+1];
	unsigned int stepNr = 0;

	while ((Action)np->step.action != NONE)
	{
		steps[stepNr++] = np->step;
		np = getNode(np->parent);
		assert(stepNr <= MAX_STEPS);
	}
	*state = initialState;
	while (stepNr)
		replayStep(state, steps[--stepNr]);
}
#endif

void dumpNodes()
{
	for (NODEI n=1; n<nodeCount; n++)
	{
		const Node* np = getNodeFast(n);
		printf("node[%d] <- %d: @%d,%d: %s\n", n, np->parent, np->step.x+1, np->step.y+1, actionNames[np->step.action]);
	}
}

// ******************************************************************************************************

INLINE HASH getHash(const State* state)
{
	return SuperFastHash((const char*)state, sizeof(State)) & ((1<<HASHSIZE)-1);
}

void printProgress();
void printFixupProgress();

NODEI addNode(const State* state, NODEI parent, Step step)
{
	#ifdef HAVE_VALIDATOR
	if (!state->validate())
		return 0;
	#endif

	HASH hash = getHash(state);
	NODEI nn;
	{
//#ifdef MULTITHREADING
//		boost::mutex::scoped_lock lock(lookupMutex[hash % PARTITIONS]);
//#endif
		NODEI old = lookup[hash];
		NODEI n = old;
		Node* prev = NULL;
		while (n)
		{
			State other;
			Node* np = getNode(n);
			replayState(np, n, &other);
#ifdef DEBUG
			if (getHash(&other) != hash)
			{
				printf("%s", state->toString());
				printf("==== %d ====\n", n);
				printf("%s", other.toString());
				error(format("Hash mismatch (%08X)", hash));
			}
#endif
			np = refreshNode(n, np);
			if (*state == other)
			{
				STATS_NODES_DO(duplicateNodes++);
				// pop node to front of hash list
				if (prev)
				{
					//assert(0, "Hash collision");
					prev->next = np->next;
					markDirty(prev);
					np->next = old;
					lookup[hash] = n;
					markDirty(np);
				}
				return n;
			}
			n = np->next;
			prev = np;
		}
		Node* np = newNode(&nn);
		lookup[hash] = nn;
		np->step = step;
		np->parent = parent;
		np->next = old;
		np->lastVisit = 0;
		//printf("node[%2d] <- %2d: @%2d,%2d: %6s (%3d)\n", nn, parent, step.x+1, step.y+1, actionNames[step.action], frame);
		STATS_NODES_DO(newNodes++);
	}
	if ((nn & 0xFFFF) == 0)
		printProgress();
	return nn;
}

NODEI findNode(const State* state)
{
	HASH hash = SuperFastHash((const char*)state, sizeof(State)) & ((1<<HASHSIZE)-1);
	NODEI n = lookup[hash];
	while (n)
	{
		State other;
		Node* np = getNode(n);
		replayState(np, n, &other);
		if (*state == other)
			return n;
		n = np->next;
	}
	return 0;
}

#if (PLAYERS>1)
const int MAX_CHILDREN = (X*Y)*2;
#else
const int MAX_CHILDREN = (X+Y)*2;
#endif

struct Child
{
	NODEI index;
	FRAME distance;
	State state;
	Step step;
};

int getChildren(NODEI n, const State* state, Child* children)
{
	Child* nextChild = children;

	struct Coord { BYTE x, y; };
	const int QUEUELENGTH = X+Y;
	Coord queue[QUEUELENGTH];
	BYTE distance[Y-2][X-2];
	uint32_t queueStart=0, queueEnd=1;
	memset(distance, 0xFF, sizeof(distance));
	
	BYTE x0 = state->players[state->activePlayer].x;
	BYTE y0 = state->players[state->activePlayer].y;
	queue[0].x = x0;
	queue[0].y = y0;
	distance[y0-1][x0-1] = 0;

	State newState = *state;
	Player* np = &newState.players[newState.activePlayer];
	while(queueStart != queueEnd)
	{
		Coord c = queue[queueStart];
		queueStart = (queueStart+1) % QUEUELENGTH;
		BYTE dist = distance[c.y-1][c.x-1];
		Step step;
		step.x = c.x-1;
		step.y = c.y-1;
		step.extraSteps = dist - (abs((int)c.x - (int)x0) + abs((int)c.y - (int)y0));

		#if (PLAYERS>1)
			np->x = c.x;
			np->y = c.y;
			int res = newState.perform(SWITCH);
			assert(res == DELAY_SWITCH);
			step.action = (unsigned)SWITCH;
			nextChild->index = addNode(&newState, n, step);
			nextChild->distance = dist * DELAY_MOVE + DELAY_SWITCH;
			nextChild->state = newState;
			nextChild->step = step;
			nextChild++; assert(nextChild-children < MAX_CHILDREN);
			newState = *state;
		#endif

		for (Action action = ACTION_FIRST; action < SWITCH; action++)
		{
			BYTE nx = c.x + DX[action];
			BYTE ny = c.y + DY[action];
			BYTE m = newState.map[ny][nx];
			if (m & OBJ_MASK)
			{
				np->x = c.x;
				np->y = c.y;
				int res = newState.perform(action);
				if (res > 0)
				{
					step.action = (unsigned)action;
					nextChild->index = addNode(&newState, n, step);
					nextChild->distance = dist * DELAY_MOVE + res;
					nextChild->state = newState;
					nextChild->step = step;
					nextChild++; assert(nextChild-children < MAX_CHILDREN);
				}
				if (res >= 0)
					newState = *state;
			}
			else
			if ((m & CELL_MASK) == 0)
				if (distance[ny-1][nx-1] == 0xFF)
				{
					distance[ny-1][nx-1] = dist+1;
					queue[queueEnd].x = nx;
					queue[queueEnd].y = ny;
					queueEnd = (queueEnd+1) % QUEUELENGTH;
					assert(queueEnd != queueStart, "Queue overflow");
				}
		}
	}
	return nextChild-children;
}

// ******************************************************************************************************

Child stackChildren[MAX_DEPTH][MAX_CHILDREN];
NODEI loopNodes = 0, fixupVisited = 0;

bool fixup(NODEI x)
{
	assert(depth < MAX_DEPTH);
	Node* xp = getNode(x);
	assert(xp->lastVisit < IN_STACK);
	assert(xp->lastVisit != NO_LOOP);
	assert(xp->lastVisit != 0);
	assert(xp->lastVisit < iteration);

	//if (verbose) writefln("Fixup: %d [%d]", x, depth);
		
	xp->lastVisit = IN_STACK + depth;
	markDirty(xp);

	int bestToFinish=xp->toFinish;
	Step bestStepToFinish;

	bool changed = false;

	stackNodes[depth] = x;
	Child* children = stackChildren[depth];
	int childCount = getChildren(x, &stackStates[depth], children);

	// "static" prescan
	for (int i=0;i<childCount;i++)
	{
		NODEI y = children[i].index;
		Node* yp = getNode(y);
		if (yp->lastVisit != NO_LOOP)
		{
			int distance = yp->toFinish==DISTANCE_INF ? DISTANCE_INF : yp->toFinish + children[i].distance;
			if (bestToFinish > distance)
			{
				bestToFinish = distance;
				bestStepToFinish = children[i].step;
				//if (verbose) writefln("Fixup: Better backref found: %d->%d (%d steps)", x, bestStepToFinish, bestToFinish);
			}
		}
	}

	xp = refreshNode(x, xp);
	if (xp->toFinish > bestToFinish)
	{
		xp->stepToFinish = bestStepToFinish;
		xp->toFinish = bestToFinish;
		changed = true;
		markDirty(xp);
	}

	// full scan
	depth++;
	for (int i=0;i<childCount;i++)
	{
		NODEI y = children[i].index;
		Node* yp = getNode(y);
		if (yp->lastVisit < IN_STACK)
		{
			if (yp->lastVisit != NO_LOOP && yp->lastVisit < iteration)
			{
				//if (verbose) writefln("Fixup: %d->%d...", x, y);
				stackStates[depth] = children[i].state;
				bool c = fixup(y);
				changed = changed || c;
				yp = refreshNode(y, yp);
				//if (verbose) { writef("Fixup: %d->%d: f=%d", x, y, yp->toFinish); if (yp->toFinish!=DISTANCE_INF) writef(" => %d", yp->stepToFinish); writefln; }
			}
			int distance = yp->toFinish==DISTANCE_INF ? DISTANCE_INF : yp->toFinish + children[i].distance;
			if (bestToFinish > distance)
			{
				bestToFinish = distance;
				bestStepToFinish = children[i].step;
				//if (verbose) writefln("Fixup: Better exit found: %d->%d (%d steps)", x, bestStepToFinish, bestToFinish);
			}
		}
	}
	depth--;

	xp = refreshNode(x, xp);
	if (xp->toFinish > bestToFinish)
	{
		xp->stepToFinish = bestStepToFinish;
		xp->toFinish = bestToFinish;
		changed = true;
	}

	xp->lastVisit = iteration;
	markDirty(xp);
	fixupVisited++;
	if ((fixupVisited & 0xFFFF)==0)
		printFixupProgress();

	//if (verbose) { writef("Fixup: Done %d [%d]: ", x, depth); if (bestToFinish!=DISTANCE_INF) writef("=>%d (%d steps)", xp->stepToFinish, bestToFinish); else writef("No exit"); writefln; fflush(stdout); }

	return changed;
}


bool search(NODEI x)
{
	enforce(depth < MAX_DEPTH, "Too deep!");
	Node* xp = getNode(x);
	if (xp->lastVisit >= IN_STACK)
		return true;
	if (xp->lastVisit != 0)
		return xp->lastVisit != NO_LOOP;
	/*static int verbose = 0;
	if (depth > 1024 && verbose==0)
		verbose = 1;
	if (verbose)
	{
		printf("=== %d [%d] ===\n%s", x, depth, stackStates[depth].toString());
		if (++verbose == 100) exit(7);
	}*/
	if (stackStates[depth].playersLeft()==0)
	{
		xp->lastVisit = NO_LOOP;
		xp->toFinish = 0;
		markDirty(xp);
		return false;
	}
	stackNodes[depth] = x;
	int bestToFinish=DISTANCE_INF;
#ifdef DEBUG
	Step bestStepToFinish = {};
#else
	Step bestStepToFinish;
#endif
	bool thisLeadsToLoop = false;
	xp->toFinish = DISTANCE_INF;
	xp->lastVisit = IN_STACK + depth;
	markDirty(xp);

	//if (verbose) writefln("At %d [%d]", x, depth);

	Child* children = stackChildren[depth];
	int childCount = getChildren(x, &stackStates[depth], children);

	depth++;
	for (int i=0;i<childCount;i++)
	{
		NODEI y = children[i].index;
		Node* yp = getNode(y);
		
		//if (verbose) writefln("%d->%d...", x, y);
		stackStates[depth] = children[i].state;
		bool f = search(y);
		//if (verbose) { writef("%d->%d: b=%s (%d), f=%d", x, y, leadsToLoop[y], backref, yp->toFinish); if (yp->toFinish!=DISTANCE_INF) writef(" => %d", yp->stepToFinish); writefln; }
		thisLeadsToLoop = thisLeadsToLoop || f;
		yp = refreshNode(y, yp);
		int distance = yp->toFinish==DISTANCE_INF ? DISTANCE_INF : yp->toFinish + children[i].distance;
		if (bestToFinish > distance)
		{
			bestToFinish = distance;
			bestStepToFinish = children[i].step;
		}
	}
	depth--;

	xp = refreshNode(x, xp);
	xp->stepToFinish = bestStepToFinish;
	xp->toFinish = bestToFinish;
	xp->lastVisit = thisLeadsToLoop ? 1 : NO_LOOP;
	markDirty(xp);
	loopNodes += (NODEI)thisLeadsToLoop;

	//if (verbose) { writef("Done %d [%d]: b=%s ", x, depth, thisLeadsToLoop); if (bestToFinish!=DISTANCE_INF) writef("=>%d (%d steps)", bestStepToFinish, bestToFinish); else writef("No exit"); writefln; fflush(stdout); }

	return thisLeadsToLoop;
}
	
// ******************************************************************************************************

void printProgress()
{
	printTime();
	printf("%d nodes, depth=%d\n", nodeCount, depth);// fflush(stdout);
}

void printFixupProgress()
{
	printf("%3d%%\x08\x08\x08\x08", fixupVisited*100LL / loopNodes);
}

void dumpChain(FILE* f, NODEI n)
{
	unsigned int totalSteps = 0;
	State state = initialState;
	FRAME frame;

	fprintf(f, "%s", state.toString());
	while (true)
	{
		const Node* np = getNodeFast(n);
		if (np->toFinish == 0)
			break;
		Step step = np->stepToFinish;
		fprintf(f, "[%d] @%d,%d: %s\n", n, step.x+1, step.y+1, actionNames[step.action]);
		totalSteps += (step.action<SWITCH ? 1 : 0) + replayStep(&state, &frame, step);
		fprintf(f, "%s", state.toString());
		n = findNode(&state);
		enforce(n, "Chain to finish is broken");
	}
	fprintf(f, "Total steps: %d", totalSteps);
}

int search()
{
	// initialize state
	memset(lookup, 0, sizeof lookup);
	
	reserveNode(); // node 0 is reserved

	Step nullStep = { (unsigned)NONE };
	NODEI first = addNode(&initialState, 0, nullStep);

	printProgress();

	iteration = 1;
	stackStates[0] = initialState;
	search(first);
	printf("Loop-node consistency: %d%%\n", loopNodes*100LL / nodeCount);
	bool changed;
	if (getNode(first)->lastVisit != NO_LOOP)
		do
		{
			iteration++;
			printTime();
			printf("Fixup iteration %d: ", iteration);
			fixupVisited = 0;
			changed = fixup(first);
			printf("Done\n");
		} while (changed);

	printf("Scan complete.\n");
	if (getNode(first)->toFinish == DISTANCE_INF)
	{
		printf("Exit not found\n");
		return 2;
	}

	FILE* f = fopen(BOOST_PP_STRINGIZE(LEVEL) ".txt", "wt");
	dumpChain(f, first);
	fclose(f);

	printf("Done.\n");
	return 0;
}
