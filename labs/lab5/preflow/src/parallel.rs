use std::sync::{Mutex,Arc, Barrier};
use std::collections::{LinkedList, VecDeque};
use std::{cmp, thread};
use std::sync::atomic::{AtomicBool, Ordering};

const DEBUG: bool = false;

macro_rules! log {
	($($arg:tt)*) => {
		if DEBUG {
			println!($($arg)*);
		}
	};
}

struct Node {
	i:	usize,				/* index of itself for debugging.	*/
	e:	i32,				/* excess preflow.			        */
	h:	usize,				/* height.				            */
	in_next: bool,			/* flag set if a node has alocated it to next round */
}

struct Edge {
    u:      usize,  
    v:      usize,
    f:      i32,
    c:      i32,
}

struct Graph {
	s:			usize,
	t:			usize,
	node:		Arc<Vec<Arc<Mutex<Node>>>>,
	edge:		Arc<Vec<Arc<Mutex<Edge>>>>,
	adj:		Arc<Vec<LinkedList<usize>>>,
}

struct Worker {
	i:			usize,
	g:			Arc<Graph>,
	excess:		Arc<Mutex<VecDeque<usize>>>,
	excess_buf: Arc<Mutex<VecDeque<usize>>>
}

impl Node {
	fn new(ii:usize) -> Node {
		Node { i: ii, e: 0, h: 0, in_next: false }
	}

}

impl Edge {
	fn new(uu:usize, vv:usize,cc:i32) -> Edge {
			Edge { u: uu, v: vv, f: 0, c: cc }      
	}
}

impl Graph {
	fn new(source: usize, sink: usize, node_c: Arc<Vec<Arc<Mutex<Node>>>>, edge_c: Arc<Vec<Arc<Mutex<Edge>>>>, adj_c: Arc<Vec<LinkedList<usize>>>) -> Self {
		Graph {
			s:		source,
			t:		sink,
			node:	node_c,
			edge:	edge_c,
			adj:	adj_c
		}
	}
}

impl Worker {
	fn new(identity: usize, graph: Arc<Graph>) -> Self {
		Worker {
			i:			identity,
			g:			graph,
			excess:	 	Arc::new(Mutex::new(VecDeque::new())),
			excess_buf: Arc::new(Mutex::new(VecDeque::new()))
		}
	}

	fn run(&self) {
		let g = &self.g;
		let node = &g.node;
		let edge = &g.edge;
		let adj = &g.adj;
		
		let mut excess = self.excess.lock().unwrap();

		while !excess.is_empty() {
			let mut pushed = false;

			let u_i = excess.pop_front().unwrap();
			let mut u_h = node[u_i].lock().unwrap().h;
			let mut u_e = node[u_i].lock().unwrap().e;
			
			let iter = adj[u_i].iter();
			
			if u_h == 0 {
				node[u_i].lock().unwrap().h += 1;
				u_h += 1;
				
				log!("@{}: relabel u{}.h from {} => {}", self.i, u_i, u_h-1, u_h);
			}

			for &e_i in iter {
				if u_e == 0 {
					break;
				}

				let v_i = other(u_i, &edge[e_i].lock().unwrap());

				
				log!("@{}: try_push: u = {}, v = {}",  self.i, u_i, v_i);
				if try_push(e_i, u_i, v_i, u_h, &mut u_e, Arc::clone(g)) {
					
					log!("@{}: pushed: u{} -> v{}, u_e = {}", self.i, u_i, v_i, u_e);
					{
						let mut node = node[v_i].lock().unwrap();
						if v_i != g.s && v_i != g.t && !node.in_next {
							node.in_next = true;
							let _ = &self.excess_buf.lock().unwrap().push_back(v_i);
							log!("@{}: add v{} to excess_buf", self.i, v_i);
						}						
						pushed = true;
					}
				}
			}

			if u_e > 0 {
				if !pushed {
					node[u_i].lock().unwrap().h += 1;
					log!("@{}: relabel u{}.h from {} => {}", self.i, u_i, u_h, u_h+1);
				}
				{
					let mut node = node[u_i].lock().unwrap();
					if !node.in_next {
						let _ = &self.excess_buf.lock().unwrap().push_back(u_i);
						node.in_next = true;
						log!("@{}: add u{} to excess_buf", self.i, u_i);
					}
				}
			}
		}
	}
}
	
fn try_push(e_i: usize, u_i: usize, v_i: usize, u_h: usize, u_e: &mut i32, g: Arc<Graph>) -> bool {
	
	if  u_h <= g.node[v_i].lock().unwrap().h {
		log!("Push failed due to height");
		return false;
	}

	let e_u = g.edge[e_i].lock().unwrap().u;
	let e_c = g.edge[e_i].lock().unwrap().c;
	let e_f = g.edge[e_i].lock().unwrap().f;

	let d: i32;
	if u_i ==  e_u{
		d = 1;
	} else {
		d = -1;
	}

	let f: i32 = cmp::min(e_c - d*e_f, *u_e);

	if f == 0 { 
		log!("Push failed df = 0");
		return false 
	}

	g.edge[e_i].lock().unwrap().f += d*f;
	g.node[v_i].lock().unwrap().e += f;
	g.node[u_i].lock().unwrap().e -= f;
	*u_e -= f;

	return true;
}

fn other(u_i: usize, e: &Edge) -> usize {
	if e.u == u_i {
		return e.v;
	} else {
		return e.u;
	}
}


pub fn run() {

	let n: usize = read!();		/* n nodes.						*/
	let m: usize = read!();		/* m edges.						*/
	let _c: usize = read!();	/* underscore avoids warning about an unused variable.	*/
	let _p: usize = read!();	/* c and p are in the input from 6railwayplanning.	*/
	let mut node = vec![];
	let mut edge = vec![];
	let mut adj: Vec<LinkedList<usize>> = Vec::with_capacity(n);
	let is_done = Arc::new(AtomicBool::new(false));

	let mut s = 0;
	let mut t = n-1;

	log!("n = {}", n);
	log!("m = {}", m);

	for i in 0..n {
		let u:Node = Node::new(i);
		node.push(Arc::new(Mutex::new(u))); 
		adj.push(LinkedList::new());
	}

	for i in 0..m {
		let u: usize = read!();
		let v: usize = read!();
		let c: i32 = read!();
		let e:Edge = Edge::new(u,v,c);
		adj[u].push_back(i);
		adj[v].push_back(i);
		edge.push(Arc::new(Mutex::new(e))); 
	}

	let node = Arc::new(node);
	let edge = Arc::new(edge);
	let adj = Arc::new(adj);
	
	for i in 0..n {
		log!("adj[{}] = ", i);
		let iter = adj[i].iter();
		
		for e in iter {
			log!("e = {}, ", e);
		}
		log!("");
	}
	
	let mut s_sum = 0; 
	for &i in adj[s].iter() {
		let e = edge[i].lock().unwrap();
		s_sum += e.c;
	}
	let mut t_sum = 0;
	for &i in adj[t].iter() {
		let e = edge[i].lock().unwrap();
		t_sum += e.c;
	}
	
	if t_sum < s_sum {
		s = n-1;
		t = 0;
	}

	let graph = Arc::new(Graph::new(
		s,
		t,
		node,
		edge,
		adj
	));
	
	{
		let mut source = graph.node[s].lock().unwrap();
		source.h = n;
	}


	let nthreads = 2;
	let mut worker: Vec<_> = vec![];
	let mut threads = vec![];

	let barrier1 = Arc::new(Barrier::new(nthreads + 1)); 
	let barrier2 = Arc::new(Barrier::new(nthreads + 1)); 

	for i in 0..nthreads {
		worker.push(Arc::new(Mutex::new(Worker::new(i, Arc::clone(&graph)))));
		let w_c = Arc::clone(&worker[i]);
		let barrier1_c = Arc::clone(&barrier1);
		let barrier2_c = Arc::clone(&barrier2);
		let is_done_c = Arc::clone(&is_done);
		let h = thread::spawn(move || {
			loop {
				// wait for main thread to be done
				log!("Worker {} waiting for main", w_c.lock().unwrap().i);
				barrier1_c.wait();
				if is_done_c.load(Ordering::Relaxed) {
					break;
				}

				w_c.lock().unwrap().run();

				// wait for all other threads to finish
				log!("Worker {} waiting for all other threads", w_c.lock().unwrap().i);
				barrier2_c.wait();
			}
		});
		threads.push(h);	
	}

	log!("initial pushes");
	let iter = graph.adj[s].iter();

	for (number, &i) in iter.enumerate() {
		let mut source = graph.node[s].lock().unwrap();
		let mut e = graph.edge[i].lock().unwrap();
		let mut other = graph.node[other(s, &e)].lock().unwrap();

		log!("pushing edge {} -> {}", source.i, other.i);
		
		source.e -= e.c;
		other.e += e.c;
		e.f = e.c;
		//add other to excess list
		if other.i != graph.t {
			worker[number%nthreads].lock().unwrap().excess.lock().unwrap().push_back(other.i);
		}
	}

	{
		log!("source excess: {}", graph.node[s].lock().unwrap().e);
	}

	loop {
		barrier1.wait();
		log!("main waiting for worker threads");
		barrier2.wait();
		log!("main start!");

		log!("current sink excess: {}, source excess: {}", graph.node[t].lock().unwrap().e, graph.node[s].lock().unwrap().e);
		if graph.node[t].lock().unwrap().e > -graph.node[s].lock().unwrap().e {
			return;
		}

		{
			if graph.node[s].lock().unwrap().e == -graph.node[t].lock().unwrap().e {
				is_done.store(true, Ordering::Relaxed);
				barrier1.wait();
				break;
			}
		}
		
		log!("main creating iterator");
		let mut all_excess_bufs = Vec::new();
		for w in worker.iter() {
			let w_lock = w.lock().unwrap();
			let mut excess_buf_lock = w_lock.excess_buf.lock().unwrap();
			all_excess_bufs.extend(excess_buf_lock.drain(..));
		}

		// let iter = worker.iter().flat_map(|w| {
		// 	w.lock().unwrap().excess_buf.lock().unwrap().drain(..)
		// });

		// for i in worker.iter() {
		// 	i.lock().unwrap().excess_buf.lock().unwrap().clear();
		// }
		log!("entering round robin loop");
		for (number, i) in all_excess_bufs.into_iter().enumerate() {
			graph.node[i].lock().unwrap().in_next = false;
			let w_c = worker[number%nthreads].lock().unwrap();
			log!("adding n{} to excess of thread{}", i, number%nthreads);
			w_c.excess.lock().unwrap().push_back(i);
			
		}
		log!("Main finished round")
	}

	for h in threads {
		log!("joining thread");
		h.join().unwrap();
	}

	let f = graph.node[t].lock().unwrap().e;

	println!("f = {}", f);
}
