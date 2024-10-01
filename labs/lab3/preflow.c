/* This is an implementation of the preflow-push algorithm, by
 * Goldberg and Tarjan, for the 2021 EDAN26 Multicore programming labs.
 *
 * It is intended to be as simple as possible to understand and is
 * not optimized in any way.
 *
 * You should NOT read everything for this course.
 *
 * Focus on what is most similar to the pseudo code, i.e., the functions
 * preflow, push, and relabel.
 *
 * Some things about C are explained which are useful for everyone  
 * for lab 3, and things you most likely want to skip have a warning 
 * saying it is only for the curious or really curious. 
 * That can safely be ignored since it is not part of this course.
 *
 * Compile and run with: make
 *
 * Enable prints by changing from 1 to 0 at PRINT below.
 *
 * Feel free to ask any questions about it on Discord 
 * at #lab0-preflow-push
 *
 * A variable or function declared with static is only visible from
 * within its file so it is a good practice to use in order to avoid
 * conflicts for names which need not be visible from other files.
 *
 */
 
#include <alloca.h>
#include <bits/pthreadtypes.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#define PRINT	1	/* enable/disable prints. */

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
typedef struct work_t work_t;
typedef struct push_t push_t;
typedef struct relabel_t relabel_t;
typedef enum WORK_TYPE {
	WORK_PUSH,
	WORK_RELABEL
} work_type;

struct list_t {
	edge_t*		edge;
	list_t*		next;
};

struct node_t {
	int			h;		/* height.			*/
	int			e;		/* excess flow.			*/
	list_t*		edge;	/* adjacency list.		*/
	node_t*		next;	/* with excess preflow.		*/
	int 		in_queue;
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
	node_t*		excess;	/* nodes with e > 0 except s,t.	*/
	work_t*		work;
};

struct work_t {
	work_type type;
	union {
		push_t* push;
		relabel_t* relabel;
	} task;
	work_t* next;
};

struct push_t {
	node_t* u;
	node_t* v;
	edge_t* e;
	int d;
};

struct relabel_t {
	node_t* u;
};

pthread_cond_t cond_main = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_worker = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int waitingWorkers = 0;
int allDone = 0;

static void* xmalloc(size_t s);

void create_push_work(work_t** work_list, node_t* u, node_t* v, edge_t* e, int df) {
	push_t* push_task = xmalloc(sizeof(push_t));
	push_task->u = u;
	push_task->v = v;
	push_task->e = e;
	push_task->d = df;

    work_t* work = xmalloc(sizeof(work_t));
    work->type = WORK_PUSH;
    work->task.push = push_task;

	work->next = *work_list;
	*work_list = work;
}

void create_relabel_work(work_t** work_list, node_t* u) {
	relabel_t* relabel_task = xmalloc(sizeof(relabel_t));
	relabel_task->u = u;

    work_t* work = xmalloc(sizeof(work_t));
    work->type = WORK_RELABEL;
    work->task.relabel = relabel_task;
    
	work->next = *work_list;
	*work_list = work;
}

void free_work_list(work_t* work_list) {
	pr("freeing work list\n");
	while (work_list != NULL) {
		if (work_list->type == WORK_PUSH)
			free(work_list->task.push);
		else
		 	free(work_list->task.relabel);
		work_t* temp = work_list;
		work_list = work_list->next;
		free(temp);
	}
}


/* a remark about C arrays. the phrase above 'array of n nodes' is using
 * the word 'array' in a general sense for any language. in C an array
 * (i.e., the technical term array in ISO C) is declared as: int x[10],
 * i.e., with [size] but for convenience most people refer to the data
 * in memory as an array here despite the graph_t's v and e members 
 * are not strictly arrays. they are pointers. once we have allocated
 * memory for the data in the ''array'' for the pointer, the syntax of
 * using an array or pointer is the same so we can refer to a node with
 *
 * 			g->v[i]
 *
 * where the -> is identical to Java's . in this expression.
 * 
 * in summary: just use the v and e as arrays.
 * 
 * a difference between C and Java is that in Java you can really not
 * have an array of nodes as we do. instead you need to have an array
 * of node references. in C we can have both arrays and local variables
 * with structs that are not allocated as with Java's new but instead
 * as any basic type such as int.
 * 
 */

static char* progname;

static int id(graph_t* g, node_t* v)
{
	/* return the node index for v.
	 *
	 * the rest is only for the curious.
	 *
	 * we convert a node pointer to its index by subtracting
	 * v and the array (which is a pointer) with all nodes.
	 *
	 * if p and q are pointers to elements of the same array,
	 * then p - q is the number of elements between p and q.
	 *
	 * we can of course also use q - p which is -(p - q)
	 *
	 * subtracting like this is only valid for pointers to the
	 * same array.
	 *
	 * what happens is a subtract instruction followed by a
	 * divide by the size of the array element.
	 *
	 */

	return v - g->v;
}

void error(const char* fmt, ...)
{
	/* print error message and exit. 
	 *
	 * it can be used as printf with formatting commands such as:
	 *
	 *	error("height is negative %d", v->h);
	 *
	 * the rest is only for the really curious. the va_list
	 * represents a compiler-specific type to handle an unknown
	 * number of arguments for this error function so that they
	 * can be passed to the vsprintf function that prints the
	 * error message to buf which is then printed to stderr.
	 *
	 * the compiler needs to keep track of which parameters are
	 * passed in integer registers, floating point registers, and
	 * which are instead written to the stack.
	 *
	 * avoid ... in performance critical code since it makes 
	 * life for optimizing compilers much more difficult. but in
	 * in error functions, they obviously are fine (unless we are
	 * sufficiently paranoid and don't want to risk an error 
	 * condition escalate and crash a car or nuclear reactor 		 
	 * instead of doing an even safer shutdown (corrupted memory
	 * can cause even more damage if we trust the stack is in good
	 * shape)).
	 *
	 */

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

	/* this is like Java's nextInt to get the next integer.
	 *
	 * we read the next integer one digit at a time which is
	 * simpler and faster than using the normal function
	 * fscanf that needs to do more work.
	 *
	 * we get the value of a digit character by subtracting '0'
	 * so the character '4' gives '4' - '0' == 4
	 *
	 * it works like this: say the next input is 124
	 * x is first 0, then 1, then 10 + 2, and then 120 + 4.
	 *
	 */

	x = 0;
        while (isdigit(c = getchar()))
                x = 10 * x + c - '0';

        return x;
}

static void* xmalloc(size_t s)
{
	void*		p;

	/* allocate s bytes from the heap and check that there was
	 * memory for our request.
	 *
	 * memory from malloc contains garbage except at the beginning
	 * of the program execution when it contains zeroes for 
	 * security reasons so that no program should read data written
	 * by a different program and user.
	 *
	 * size_t is an unsigned integer type (printed with %zu and
	 * not %d as for int).
	 *
	 */

	p = malloc(s);

	if (p == NULL)
		error("out of memory: malloc(%zu) failed", s);

	return p;
}

static void* xcalloc(size_t n, size_t s)
{
	void*		p;

	p = xmalloc(n * s);

	/* memset sets everything (in this case) to 0. */
	memset(p, 0, n * s);

	/* for the curious: so memset is equivalent to a simple
	 * loop but a call to memset needs less memory, and also
 	 * most computers have special instructions to zero cache 
	 * blocks which usually are used by memset since it normally
	 * is written in assembler code. note that good compilers 
	 * decide themselves whether to use memset or a for-loop
	 * so it often does not matter. for small amounts of memory
	 * such as a few bytes, good compilers will just use a 
	 * sequence of store instructions and no call or loop at all.
	 *
	 */

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
	pr("locking mutex %s\n", name);
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
		pr("%d", sinkTotalFlow);
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

static void push(graph_t* g, node_t* u, node_t* v, edge_t* e, int df)
{

	pr("push from %d to %d: ", id(g, u), id(g, v));
	pr("f = %d, c = %d, so pushing %d\n", e->f, e->c, df);

	u->e -= abs(df);
	v->e += abs(df);
	e->f += df;
}

static void relabel(graph_t* g, node_t* u)
{
	u->h += 1;
	pr("relabel %d now h = %d\n", id(g, u), u->h);
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

void allocateNodeToThread(graph_t* g, node_t* u)
{
	if (u != g->s && u != g->t && u->in_queue == 0) {
		u->in_queue = 1;
		int index = getNextThreadIndex(g);
		worker_t* worker = &g->worker[index];
		u->next = worker->excess;
		worker->excess = u;
		pr("added node %d to thread %d\n", id(g, u), index);
	} else {
		pr("skipping adding s or t to excess list\n");
	}
}

void *work(void* args)
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

	while (1) {
		node_t* u = worker->excess;
		while (u != NULL) {
			u_e = u->e; //excess flow of u
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

				if (u->h == 0) {
					//do reabel
					pr("create relabel work for node @%d\n", id(g, u));
					create_relabel_work(&worker->work, u);
					pushed = 1;
					break;
				}

				if (u->h > v->h && abs(u->e) > 0 && b * e->f < e->c) {
					if (u == e->u) {
						df = MIN(u_e, e->c - e->f);
					} else {
						df = MIN(u_e, e->c + e->f);
					}
					u_e -= df;
					create_push_work(&worker->work, u, v, e, df);
					pr("@T%d: create push work from node @%d to node @%d, df = %d\n", worker->i, id(g, u), id(g, v), df);
					pushed = 1;
				}
			}


			//2. if not pushed, reabel
			if (!pushed){
				create_relabel_work(&worker->work, u);
			}
			node_t* temp = u;
			u = u->next;
			temp->in_queue = 0;
			temp->next = NULL;
		}

		worker->excess = NULL;

		pthread_mutex_lock(&mutex);
		waitingWorkers++;

		pthread_cond_signal(&cond_main);

		pr("Thread %d waiting\n", worker->i);
		pthread_cond_wait(&cond_worker, &mutex);
		pr("Thread %d wakes up\n", worker->i);

		if (allDone) {
			pthread_mutex_unlock(&mutex);
			return 0;
		}

		pthread_mutex_unlock(&mutex);
	}
}
	
int preflow(graph_t* g)
{
	node_t*		s;
	node_t*		t;
	node_t*		u;
	node_t*		v;
	edge_t*		e;
	list_t*		p;

	int nthreads = g->nthreads;
	
	s = g->s;
	t = g->t;
	
	// Set source height
	s->h = g->n;

	// Set pointer to first source edge
	p = s->edge;

	/* start by pushing as much as possible (limited by
	 * the edge capacity) from the source to its neighbors.
	 *
	 */
	pthread_t thread[nthreads];

	//  Start by pushing from source
	int totalPushed = 0;
	int first = 1;
	while (p != NULL) {
		e = p->edge;
		p = p->next;
		
		int df;
		if (s == e->u) {
			df = e->c;
		} else {
			df = -e->c;
		}

		node_t* v = other(s, e);
		pr("push from %d to %d: ", id(g, s), id(g, v));
		pr("f = %d, c = %d, so pushing %d\n", e->f, e->c, df);
		v->e += abs(df);
		e->f += df;
		

		totalPushed += e->c;
		allocateNodeToThread(g, v);
	}



	s->e -= totalPushed;
	
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

		int first = 1;
		for (int i = 0; i < nthreads; i++) {
			work_t* w = g->worker[i].work;
			while (w != NULL) {
				if (w->type == WORK_PUSH) {
					push(g, w->task.push->u, w->task.push->v, w->task.push->e, w->task.push->d);
					allocateNodeToThread(g, w->task.push->v);
					if (w->task.push->u->e > 0)
						allocateNodeToThread(g, w->task.push->u);
					first = 0;
				} else {
					relabel(g, w->task.relabel->u);
					allocateNodeToThread(g, w->task.relabel->u);
					first = 0;
				}
				w = w->next;
			}
			free_work_list(g->worker[i].work);
			g->worker[i].work = NULL;
		}

		if (first) {
			pr("error: s->e = %d, t->e = %d\n", s->e, t->e);
			return -1;
		}

		if (-s->e == t->e) {
			allDone = 1;
			pthread_mutex_unlock(&mutex);
			pthread_cond_broadcast(&cond_worker);
			pr("All done: s->e = %d, t->e = %d\n", s->e, t->e);
			break;
		}

		waitingWorkers = 0;
		pthread_mutex_unlock(&mutex);

		pthread_cond_broadcast(&cond_worker);
	}

	for (int i = 0; i< nthreads; i += 1) {
		pr("joining thread %d\n", i);
		if (pthread_join(thread[i], NULL) != 0)  {
			error("pthread_join failed");
		}
	}

	return t->e;
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
