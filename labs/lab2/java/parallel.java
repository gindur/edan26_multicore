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
	
	synchronized void allocateWorkToNextThread(Node v) {
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
		lockNodes(u,v);
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
		
		unlockNodes(u,v);
		return pushed;
	}

	private void lockNodes(Node u, Node v){
		if (u.i < v.i){
			u.lock();
			v.lock();
		} else {
			v.lock();
			u.lock();
		}
	}

	private void unlockNodes(Node u, Node v){
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

		for (int i = 0; i < nthreads; ++i){
			work[i].start();
			log("Starting thread " + i);
		}


		
		// lock source
		node[s].lock();
		try {
			iter = node[s].adj.listIterator();
			node[s].h = n;
			for (int i = 0; iter.hasNext(); ++i) {
				i %= nthreads;
				a = iter.next();

				node[s].e += a.c;

				push(node[s], other(a, node[s]), a);
				
				work[i].addExcess(other(a, node[s]));
				// unlock node[i]
			}
		} finally {
			// unlock source
			node[s].unlock();
		}

		for (int i = 0; i < nthreads; ++i)
			try {
				work[i].join();
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
	Node excess; 	// nodes with excess preflow Node s
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
		Node u = excess;
		Node v = null;
		Edge a = null;

		while (true) {
			synchronized (this) {
				if (u == null) {
					break;
				}
			}
			boolean pushed = false;

			ListIterator<Edge> iter = u.adj.listIterator();
			while (iter.hasNext()) {
				a = iter.next();
				v = g.other(a, u);

				Graph.log("Tried pushing to @v" + v.i);
				if (g.push(u, v, a)) {
					Graph.log("Pushed to node @v" + v.i +" from @v" + u.i);
					pushed = true;
					g.allocateWorkToNextThread(v);
				}
			}

			if (!pushed)
				u.relabel();
			
			if (u.e == 0)
				u = u.next;
			
    	} 
	}

	public void push(Node u, Node v, Edge a){
		g.push(u, v, a);
	}

	public synchronized void addExcess(Node u) {
		u.next = this.excess;
		this.excess = u;
		Graph.log("Adding node @v" + u.i + " to excess. " + excess);
		run();
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

		System.out.println("Starting main");

		n = s.nextInt();
		m = s.nextInt();
		s.nextInt();
		s.nextInt();
		System.out.println("Starting main 2");
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
		System.out.println("Created graph");

		f = g.preflow(0, n-1, nthreads);
		double	end = System.currentTimeMillis();
		System.out.println("t = " + (end - begin) / 1000.0 + " s");
		System.out.println("f = " + f);
	}
}
