/*
BARRIER + ATOMIC variables (instant relable and push in threads)
*/
 
#include <alloca.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <stdatomic.h>

#define PRINT	0	/* enable/disable prints. */

/* the funny do-while next clearly performs one iteration of the loop.
 * if you are really curious about why there is a loop, please check
 * the course book about the C preprocessor where it is explained. it
 * is to avoid bugs and/or syntax errors in case you use the pr in an
 * if-statement without { }.
 *
 */

#if PRINT
#define pr(...)		do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define pr(...)		/* no effect at all */
#endif

#define MIN(a,b)	(((a)<=(b))?(a):(b))

/* introduce names for some structs. a struct is like a class, except
 * it cannot be extended and has no member methods, and everything is
 * public.
 *
 * using typedef like this means we can avoid writing 'struct' in 
 * every declaration. no new type is introduded and only a shorter name.
 *
 */

typedef struct graph_t	graph_t;
typedef struct node_t	node_t;
typedef struct edge_t	edge_t;
typedef struct list_t	list_t;
typedef struct worker_t worker_t;

typedef struct xedge_t	xedge_t;
struct xedge_t {
	int		u;	/* one of the two nodes.	*/
	int		v;	/* the other. 			*/
	int		c;	/* capacity.			*/
};





struct list_t {
	edge_t*		edge;
	list_t*		next;
};

struct node_t {
	atomic_int			h;		/* height.			*/
	atomic_int			e;		/* excess flow.			*/
	list_t*		edge;	/* adjacency list.		*/
	node_t*		next;	/* with excess preflow.		*/
	int 		in_queue;
	node_t*     next_delta_e;
	atomic_flag 		has_delta_e;
};

struct edge_t {
	node_t*		u;	/* one of the two nodes.	*/
	node_t*		v;	/* the other. 			*/
	int			f;	/* flow > 0 if from u to v.	*/
	int			c;	/* capacity.			*/
};

struct graph_t {
	int		n;	/* nodes.			*/
	int		m;	/* edges.			*/
	int    	nthreads;
	int     totalJobs;
	worker_t* 	worker;
	node_t*		v;	/* array of n nodes.		*/
	edge_t*		e;	/* array of m edges.		*/
	node_t*		s;	/* source.			*/
	node_t*		t;	/* sink.			*/
};

struct worker_t {
	int			i;
	graph_t*	g;
	node_t*     next_round;
	node_t*		excess;	/* nodes with e > 0 except s,t.	*/
};

pthread_cond_t cond_main = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_worker = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int waitingWorkers;
int allDone;


static void* xmalloc(size_t s);

#ifdef MAIN
static graph_t* new_graph(FILE* in, int n, int m);
#else
static graph_t* new_graph(int n, int m, int s, int t, xedge_t* e, int nthreads);
#endif

static char* progname;

static int id(graph_t* g, node_t* v)
{
	return v - g->v;
}

void error(const char* fmt, ...)
{

	va_list		ap;
	char		buf[BUFSIZ];

	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);

	if (progname != NULL)
		fprintf(stderr, "%s: ", progname);

	fprintf(stderr, "error: %s\n", buf);
	exit(1);
}

static int next_int()
{
        int     x;
        int     c;

	x = 0;
        while (isdigit(c = getchar()))
                x = 10 * x + c - '0';

        return x;
}

static void* xmalloc(size_t s)
{
	void*		p;

	p = malloc(s);

	if (p == NULL)
		error("out of memory: malloc(%zu) failed", s);

	return p;
}

static void* xcalloc(size_t n, size_t s)
{
	void*		p;

	p = xmalloc(n * s);

	memset(p, 0, n * s);


	return p;
}

static void mutex_lock(pthread_mutex_t* m, const char* name)
{
	/* lock a mutex and check that it was successful.
	 *
	 * the pthread_mutex_lock function returns 0 if the lock
	 * was successful and otherwise an error code.
	 *
	 */
	// pr("locking mutex %s\n", name);
	if (pthread_mutex_lock(m) != 0)
		error("mutex lock failed");

}

static void add_edge(node_t* u, edge_t* e)
{
	list_t*		p;

	/* allocate memory for a list link and put it first
	 * in the adjacency list of u.
	 *
	 */

	p = xmalloc(sizeof(list_t));
	p->edge = e;
	p->next = u->edge;
	u->edge = p;
}

static void connect(node_t* u, node_t* v, int c, edge_t* e)
{
	/* connect two nodes by putting a shared (same object)
	 * in their adjacency lists.
	 *
	 */

	e->u = u;
	e->v = v;
	e->c = c;

	add_edge(u, e);
	add_edge(v, e);
}


static void relabel(graph_t* g, node_t* u)
{
	u->h += 1;
	//pr("relabel %d now h = %d\n", id(g, u), u->h);
}

static node_t* other(node_t* u, edge_t* e)
{
	if (u == e->u)
		return e->v;
	else
		return e->u;
}

static int getNextThreadIndex(graph_t* g)
{
	int index = g->totalJobs % g->nthreads;
	g->totalJobs += 1;
	return index;
}

static void allocateNodeToThread(graph_t* g, node_t* u)
{
	if (u != g->s && u != g->t && u->in_queue == 0) {
		u->in_queue = 1;
		int index = getNextThreadIndex(g);
		worker_t* worker = &g->worker[index];
		u->next = worker->excess;
		worker->excess = u;
		//pr("added node %d with excess %d to thread %d\n", id(g, u),u->e, index);
	} else {
		//pr("skipping adding s or t to excess list\n");
	}
}

static void *work(void* args)
{	
	/* loop until only s and/or t have excess preflow. */
	worker_t* worker = (worker_t*) args;
	node_t* v = NULL;
	edge_t* e = NULL;
	graph_t* g = worker->g;
	list_t* p;
	int		b;
	int 	df;
	int 	u_e; 
	int 	u_h;


	while (1) {
		node_t* u = worker->excess;
		while (u != NULL) {
			pr("@T%d: looking at node %d\n", worker->i, id(g, u));
			u_e = atomic_load(&u->e); //excess flow of u
			u_h = atomic_load(&u->h);
			p = u->edge;
			int pushed = 0;

			//1. check if push is possible
			while (p != NULL && u_e > 0) {
				e = p->edge;
				p = p->next;

				if (u == e->u) {
					v = e->v;
					b = 1;
				} else {
					v = e->u;
					b = -1;
				}

				pr("@T%d: looking at next edge %d -> %d, u_e = %d\n", worker->i, id(g, u), id(g, v), u_e);

				if (u_h == 0) {
					//do reabel
					//pr("create relabel work for node @%d\n", id(g, u));
					// create relabel work
					atomic_fetch_add_explicit(&u->h, 1 , memory_order_relaxed);
					pushed = 1;
					break;
				}
				// print if statement
				//pr("@T%d: chckecking if: u->h = %d > v->h = %d && abs(u_e) = %d > 0 && b * e->f = %d < e->c = %d\n", worker->i, u->h, v->h, abs(u_e), b*e->f, e->c);
				if (u_h > atomic_load(&v->h) && abs(u_e) > 0 && b * e->f < e->c) {
					if (b ==  1) {
						df = MIN(u_e, e->c - e->f);
					} else {
						df = -MIN(u_e, e->c + e->f); //This flow must be negative
					}
					u_e -= abs(df);
					// push atomically
					atomic_fetch_add_explicit(&v->e, abs(df), memory_order_relaxed);
					atomic_fetch_add_explicit(&u->e, -abs(df), memory_order_relaxed);

					// Create push work
					// atomic_fetch_add_explicit(&delta_excess[id(g, v)], abs(df), memory_order_relaxed);
					// atomic_fetch_add_explicit(&delta_excess[id(g, u)], -abs(df), memory_order_relaxed);
					e->f += df;
					//pr("@T%d: create push work from node @%d to node @%d, df = %d\n", worker->i, id(g, u), id(g, v), df);
					pushed = 1;
					int expected = 0;
					// test and set for atomic flags
					if (!atomic_flag_test_and_set(&v->has_delta_e)){
						v->next_delta_e = worker->next_round;
						worker->next_round = v;
					}
				}
			}


			if (u_e> 0){
				if (!pushed)
					atomic_fetch_add_explicit(&u->h, 1 , memory_order_relaxed);
				if (!atomic_flag_test_and_set(&u->has_delta_e)){
					u->next_delta_e = worker->next_round;
					worker->next_round = u;
				}
			}
			node_t* temp = u;
			u = u->next;
			temp->in_queue = 0;
			//2. if not pushed, reabel
			temp->next = NULL;

				



		}

		worker->excess = NULL;

		pthread_mutex_lock(&mutex);
		waitingWorkers++;

		pthread_cond_signal(&cond_main);

		//pr("Thread %d waiting\n", worker->i);
		pthread_cond_wait(&cond_worker, &mutex);
		//pr("Thread %d wakes up\n", worker->i);

		if (allDone) {
			pthread_mutex_unlock(&mutex);
			return 0;
		}

		pthread_mutex_unlock(&mutex);
	}
}

int preflow(int n, int m, int s, int t, xedge_t* e)
{
	node_t*		ns;
	node_t*		nt;
	node_t*		nu;
	node_t*		nv;
	edge_t*		ee;
	list_t*		lp;
	
	graph_t*	g;

	allDone = 0;
	waitingWorkers = 0;


	int nthreads = 30;
	g = new_graph(n, m, s, t, e, nthreads);
	
	ns = g->s;
	nt = g->t;

	
	// Set source height
	ns->h = g->n;

	// Set pointer to first source edge
	lp = ns->edge;

	pthread_t thread[nthreads];

	int totalPushed = 0;
	int first = 1;
	while (lp != NULL) {
		ee = lp->edge;
		lp = lp->next;
		
		int df;
		if (ns == ee->u) {
			df = ee->c;
		} else {
			df = -ee->c;
		}

		nv = other(ns, ee);
		
		nv->e += abs(df);
		ee->f += df;
		

		totalPushed += ee->c;
		allocateNodeToThread(g, nv);
	}



	ns->e -= totalPushed;
	
	// Start working threads
	for (int i = 0; i < nthreads; i += 1) {
		if (pthread_create(&thread[i], NULL, work, (void *) &g->worker[i])) {
			error("pthread_create failed");
		}
	}

	while(1) {
        pthread_mutex_lock(&mutex);
		while (waitingWorkers < nthreads) {
			pthread_cond_wait(&cond_main, &mutex);
		}
		
		if (-ns->e == nt->e) {
			allDone = 1;
			pthread_cond_broadcast(&cond_worker);
			pthread_mutex_unlock(&mutex);
			break;
		}
		for (int i = 0; i < g->nthreads; i++) {
			node_t* node = g->worker[i].next_round;
			while (node != NULL){
				allocateNodeToThread(g, node);
				node_t* temp = node;
				atomic_flag_clear(&node->has_delta_e);
				node = node->next_delta_e;
				temp->next_delta_e = NULL;
			}
			g->worker[i].next_round = NULL;
		}


		waitingWorkers = 0;
		pthread_cond_broadcast(&cond_worker);
		pthread_mutex_unlock(&mutex);

	}

	for (int i = 0; i< nthreads; i += 1) {
		//pr("joining thread %d\n", i);
		if (pthread_join(thread[i], NULL) != 0)  {
			error("pthread_join failed");
		}
	}
	
	return nt->e;
}

static void free_graph(graph_t* g)
{
	int			i;
	list_t*		p;
	list_t*		q;


	for (i = 0; i < g->n; i += 1) {
		p = g->v[i].edge;
		while (p != NULL) {
			q = p->next;
			free(p);
			p = q;
		}
	}
	free(g->v);
	free(g->e);
	free(g);
}

#ifdef MAIN

static graph_t* new_graph(FILE* in, int n, int m, int nthreads)
{
	graph_t*	g;
	node_t*		u;
	node_t*		v;
	int		i;
	int		a;
	int		b;
	int		c;
	
	g = xmalloc(sizeof(graph_t));

	g->n = n;
	g->m = m;
	
	g->v = xcalloc(n, sizeof(node_t));
	g->e = xcalloc(m, sizeof(edge_t));

	g->totalJobs = 0;
	g->nthreads = nthreads;

	g->worker = xcalloc(nthreads, sizeof(worker_t));
	for (int i = 0; i < nthreads; i += 1) {
		g->worker[i].i = i;
		g->worker[i].g = g;
	}

	for (i = 0; i < m; i += 1) {
		a = next_int();
		b = next_int();
		c = next_int();
		u = &g->v[a];
		v = &g->v[b];
		atomic_flag_clear(&u->has_delta_e);
		atomic_flag_clear(&v->has_delta_e);
		connect(u, v, c, &g->e[i]);
	}

	// switch source and sink here if sounce flow is more than sink flow
	list_t * p = g->v[0].edge;
	int sourceTotalFlow = 0;
	while (p != NULL) {
		sourceTotalFlow += p->edge->c;
		p = p->next;
	}

	list_t* q = g->v[n-1].edge;
	int sinkTotalFlow = 0;
	while (q != NULL) {
		sinkTotalFlow += q->edge->c;
		//pr("%d", sinkTotalFlow);
		q = q->next;
	}

	if (sinkTotalFlow < sourceTotalFlow) {
		g->s = &g->v[n-1];
		g->t = &g->v[0];
	} else {
		g->s = &g->v[0];
		g->t = &g->v[n-1];
	}


	return g;
}

int main(int argc, char* argv[])
{
	FILE*		in;	/* input file set to stdin	*/
	graph_t*	g;	/* undirected graph. 		*/
	int		f;	/* output from preflow.		*/
	int		n;	/* number of nodes.		*/
	int		m;	/* number of edges.		*/

	progname = argv[0];	/* name is a string in argv[0]. */

	in = stdin;		/* same as System.in in Java.	*/

	n = next_int();
	m = next_int();

	/* skip C and P from the 6railwayplanning lab in EDAF05 */
	next_int();
	next_int();

	int nthreads = 2;

	g = new_graph(in, n, m, nthreads);

	fclose(in);

	f = preflow(g);

	printf("f = %d\n", f);

	free_graph(g);

	return 0;
}

#else

static graph_t* new_graph(int n, int m, int s, int t, xedge_t* e, int nthreads)
{
	graph_t*	g;
	node_t*		u;
	node_t*		v;
	int		i;
	int		a;
	int		b;
	int		c;

	g = xmalloc(sizeof(graph_t));
	g->n = n;
	g->m = m;
	g->v = xcalloc(n, sizeof(node_t));
	g->e = xcalloc(m, sizeof(edge_t));

	g->totalJobs = 0;
	g->nthreads = nthreads;

	g->worker = xcalloc(nthreads, sizeof(worker_t));
	for (int i = 0; i < nthreads; i += 1) {
		g->worker[i].i = i;
		g->worker[i].g = g;
	}

	for (i = 0; i < m; i += 1) {
		a = e[i].u;
		b = e[i].v;
		c = e[i].c;
		u = &g->v[a];
		v = &g->v[b];
		atomic_flag_clear(&u->has_delta_e);
		atomic_flag_clear(&v->has_delta_e);
		connect(u, v, c, &g->e[i]);
	}

	// switch source and sink here if sounce flow is more than sink flow
	list_t * p = g->v[s].edge;
	int sourceTotalFlow = 0;
	while (p != NULL) {
		sourceTotalFlow += p->edge->c;
		p = p->next;
	}

	list_t* q = g->v[t].edge;
	int sinkTotalFlow = 0;
	while (q != NULL) {
		sinkTotalFlow += q->edge->c;
		//pr("%d", sinkTotalFlow);
		q = q->next;
	}

	if (sinkTotalFlow < sourceTotalFlow) {
		g->s = &g->v[t];
		g->t = &g->v[s];
	} else {
		g->s = &g->v[s];
		g->t = &g->v[t];
	}
	return g;
}

#endif
