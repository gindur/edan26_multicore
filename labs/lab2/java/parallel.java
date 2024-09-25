import java.util.Scanner;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.Iterator;
import java.util.ListIterator;
import java.util.LinkedList;

import java.io.*;


class Graph {
	int	s;
	int	t;
	int	n;
	int	m;
	int nthreads;
	static boolean debug = false;
	static boolean debug2 = false;


	Node	node[];
	Edge	edge[];

	int totalJobs = 0;
	Work work[];

	Graph(Node node[], Edge edge[])
	{
		this.node	= node;
		this.n		= node.length;
		this.edge	= edge;
		this.m		= edge.length;
	}

	static void log(String s) {
		if (debug)
			System.out.println(s);
	}

	static void log2(String s) {
		if (debug2)
			System.out.println(s);
	}
	
	synchronized void allocateWorkToNextThread(Node v) {
		Graph.log("Allocating work to next thread for node @v" + v.i);
		if (v.i != s && v.i != t) {
			totalJobs++;
			work[totalJobs % nthreads].addExcess(v);
		}
	}

	Node other(Edge a, Node u)
	{
		if (a.u == u)	
			return a.v;
		else
			return a.u;
	}

	private int getPerspectiveFlow(Node n, Edge e) {
		if (n == e.u)
			return e.f;
		else
			return -e.f;
	}

	boolean push(Node u, Node v, Edge a)
	{
		boolean pushed = false;
		// lockNodes(u,v);

		if (u.h == 0) {
			u.h = 1;
		}

		if (u.h > v.h && Math.abs(u.e) > 0 && getPerspectiveFlow(u, a) < a.c){
			int df = Math.min(u.e, a.c - getPerspectiveFlow(u, a));
			if (a.u == u)
				a.f += df;
			else
				a.f -= df;

			u.changeExcess(-df);
			v.changeExcess(df);
			log("Pushing from @v" + u.i + " h: "+ u.h + " to @v" + v.i + " h: " + v.h + " with excess: " + u.e + " and capacity: " + a.c + " and flow: " + df);
			pushed = true;
		}
		
		// unlockNodes(u,v);
		return pushed;
	}

	void lockNodes(Node u, Node v){
		if (u.i < v.i){
			u.lock();
			v.lock();
		} else {
			v.lock();
			u.lock();
		}
	}

	void unlockNodes(Node u, Node v){
		if (u.i < v.i){
			u.unlock();
			v.unlock();
		} else {
			v.unlock();
			u.unlock();
		}
	}

	int preflow(int s, int t, int nthreads)
	{
		ListIterator<Edge>	iter;
		Edge			a;
		
		this.s = s;
		this.t = t;
		this.nthreads = nthreads;
		node[s].h = n;

		this.work = new Work[nthreads];
		
		for (int i = 0; i < nthreads; ++i) {
			work[i] = new Work(node[s], node[t], this);
			log("Creating thread " + i);
		}

		// lock source
		node[s].lock();
		try {
			iter = node[s].adj.listIterator();
			node[s].h = n;
			while (iter.hasNext()) {
				a = iter.next();
				Node other = other(a, node[s]);
				
				if (a.u == node[s])
				a.f = a.c;
				else
				a.f = -a.c;
				
				node[s].e -= a.c;
				other.e += a.c;
				
				allocateWorkToNextThread(other);
			}
		} finally {
			// unlock source
			node[s].unlock();
		}
		
		for (int i = 0; i < nthreads; ++i){
			work[i].start();
		}

		Graph.log("All nodes pushed from source");

		double start = System.currentTimeMillis();
		int seconds = 0;
		while (true) {
			int sf = node[s].e;
			int tf = node[t].e;

			if ((System.currentTimeMillis() - start)/50 > seconds) {
				seconds++;
				String threadStates = "Thread states:\n";
				for (int i = 0; i < nthreads; ++i) {
					threadStates += work[i].getName() + ": " + work[i].getState() + ", has queue: " + (work[i].excessHead != null) + "\n";
				}
				String nodesWIthExcess = "Nodes with excess:\n";
				for (int i = 0; i < n; ++i) {
					if (node[i].e > 0) {
						nodesWIthExcess += "Node @v" + i + " excess: " + node[i].e + " inQueue: " + node[i].inQueue + "\n";
					}
				}

				Graph.log2("Time: " + seconds + " s/20");
				Graph.log2("sf: " + sf + ", tf: " + tf);
				Graph.log2(threadStates);
				Graph.log2(nodesWIthExcess);
			}


			if (Math.abs(sf) == tf) {
				for (int i = 0; i < nthreads; ++i)
					work[i].interrupt();	
				break;
			}
		}

		for (int i = 0; i < nthreads; ++i)
			try {
				work[i].join();
				Graph.log(work[i].getName() + " joined");
			} catch (Exception e) {
				System.out.println("" + e);
			}

		return node[t].e;
	}
}

class Node {
	int		h;
	int		e;
	int		i;
	Node	next;
	Node	prev;
	Boolean	inQueue = false;
	LinkedList<Edge>	adj;
	private ReentrantLock lock;

	Node(int i)
	{
		this.i = i;
		adj = new LinkedList<Edge>();
		lock = new ReentrantLock();
		h = 0;
	}

	public void lock(){
		lock.lock();
	}

	public void unlock(){
		lock.unlock();
	}

	public void changeExcess(int f) {
		e += f;
		Graph.log("Changing excess of @v" + i + " to " + e);
	}

	public synchronized void relabel() {
		Graph.log("Relabeling node @v" + i + " from " + h + " to " + (h + 1));
		h += 1;
	}
}

class Edge {
	Node	u;
	Node	v;
	int		f;
	int		c;

	Edge(Node u, Node v, int c)
	{
		this.u = u;
		this.v = v;
		this.c = c;

	}

}

class Work extends Thread {
	Node excessHead; 	// nodes with excess preflow Node s
	Node excessTail;
	Node s; 		// source node
	Node t; 		// sink node
	Graph g;

	public Work(Node s, Node t, Graph g) {
		this.s = s;
		this.t = t;
		this.g = g;
		Graph.log("Thread responding");
	}
	
	public void run() {
		Graph.log(Thread.currentThread().getName() + " running");
		Node u = null;
		Node v = null;
		Edge a = null;
		
		while (true) {
			synchronized (this) {
				u = excessHead;
				if (u == null) {
					Graph.log(Thread.currentThread().getName() + " waiting");
					try {
						this.wait();
						Graph.log(Thread.currentThread().getName() + " woke up");
						continue;
					} catch (InterruptedException e) {
						Thread.currentThread().interrupt();
						Graph.log("Interrupted");
						break;
					}
				}
			}

			boolean pushed = false;

			ListIterator<Edge> iter = u.adj.listIterator();
			int len = u.adj.size();
			int c = 1;
			while (iter.hasNext() && u.e > 0) {
				a = iter.next();
				v = g.other(a, u);

				Graph.log("Tried pushing edge " + c++ + "/" + len + ": @u" + u.i + " -> @v" + v.i);
				Graph.log("@u" + u.i + " (h: " + u.h + ", e: " + u.e + "), @v" + v.i + " (h: "+ v.h + ", e: " + v.e + ")");
				Graph.log("edge flow/cap: " + a.f + "/" + a.c + "\n");
				g.lockNodes(u, v);
				if (g.push(u, v, a)) {
					Graph.log("Pushed to node @v" + v.i +" from @v" + u.i);
					pushed = true;
					
					g.allocateWorkToNextThread(v);
				}
				g.unlockNodes(u, v);
			}

			if (!pushed && u.e > 0)
				u.relabel();

			synchronized(this) {
				if (u.e == 0) {
					Graph.log("@u" + u.i + " has no excess, go to next node @u" + (u.next != null ? u.next.i : "NULL"));
	
					// remove from excess list
					excessHead = u.next;
					u.next = null;
					if (excessHead != null)
						excessHead.prev = null;
					else
						excessTail = null;

					u.inQueue = false;
				}
			}			
    	} 
	}

	public void push(Node u, Node v, Edge a){
		g.push(u, v, a);
	}

	public synchronized void addExcess(Node u) {
		Graph.log("Trying to add node @v" + u.i + " to excess.");
		if (u.inQueue) {
			Graph.log("Node @v" + u.i + " already in excess.");
			return;
		}

		u.inQueue = true;

		if (excessHead == null) {
			excessHead = u;
			excessTail = u;
		} else {
			excessTail.next = u;
			excessTail = u;
		}
		Graph.log("Adding node @v" + u.i + " to excess.");
		synchronized (this) {
			this.notify();
		}

		// if (excess == null || u.i != excess.i) {
		// 	if (u.inQueue) {
		// 		// Exists in other excess
		// 		return;
		// 	}
		// 	u.next = this.excess;
		// 	this.excess = u;
		// 	Graph.log("Adding node @v" + u.i + " to excess. " + excess);
		// 	run();
		// }
	}
}

class Preflow {
	public static void main(String args[])
	{
		double	begin = System.currentTimeMillis();
		Scanner s = new Scanner(System.in);
		int	n;
		int	m;
		int	i;
		int	u;
		int	v;
		int	c;
		int	f;
		int nthreads = 10;
		Graph	g;

		n = s.nextInt();
		m = s.nextInt();
		s.nextInt();
		s.nextInt();

		Node[] node = new Node[n];
		Edge[] edge = new Edge[m];
		for (i = 0; i < n; i += 1)
			node[i] = new Node(i);

		for (i = 0; i < m; i += 1) {
			u = s.nextInt();
			v = s.nextInt();
			c = s.nextInt(); 
			edge[i] = new Edge(node[u], node[v], c);
			node[u].adj.addLast(edge[i]);
			node[v].adj.addLast(edge[i]);
		}

		g = new Graph(node, edge);
		Graph.log("Created graph");
		f = -1;

		try {
			f = g.preflow(0, n-1, nthreads);
		} catch (Exception e) {
			System.out.println("" + e.getMessage());
		}
		double	end = System.currentTimeMillis();
		System.out.println("t = " + (end - begin) / 1000.0 + " s");
		System.out.println("f = " + f);
	}
}
