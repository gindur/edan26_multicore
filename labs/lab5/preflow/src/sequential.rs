use std::sync::{Mutex,Arc};
use std::collections::LinkedList;
use std::cmp;
use std::collections::VecDeque;

const DEBUG: bool = false;

struct Node {
	i:	usize,				/* index of itself for debugging.	*/
	e:	i32,				/* excess preflow.			        */
	h:	i32,				/* height.				            */
}

struct Edge {
    u:      usize,  
    v:      usize,
    f:      i32,
    c:      i32,
}



impl Node {
	fn new(ii:usize) -> Node {
		Node { i: ii, e: 0, h: 0 }
	}

}

impl Edge {
	fn new(uu:usize, vv:usize,cc:i32) -> Edge {
			Edge { u: uu, v: vv, f: 0, c: cc }      
	}
}

macro_rules! log {
    ($($arg:tt)*) => {
        if DEBUG {
            println!($($arg)*);
        }
    };
}
	
fn try_push(edge:&mut Edge, u:&mut Node, v:&mut Node) -> bool {
	log!("try_push: u = {}, v = {}", u.i, v.i);
	if u.h <= v.h {
		log!("Push failed due to height");
		return false;
	}

	let d: i32;
	if u.i ==  edge.u{
		d = 1;
	} else {
		d = -1;
	}

	let f: i32 = cmp::min(edge.c - d*edge.f, u.e as i32);

	if f == 0 { 
		log!("Push failed df = 0");
		return false 
	}

	edge.f += d*f;
	u.e -= f;
	v.e += f;

	log!("Push success df = {}", f);
	return true;
}

fn relabel(u: &mut Node) {
	u.h += 1;
	log!("relabel u{}.h = {}", u.i, u.h);
}

fn other(u: &Node, e: &Edge) -> usize {
	if e.u == u.i {
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
	let mut excess: VecDeque<usize> = VecDeque::new();

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
	
	let iter = adj[s].iter();
	
	{
		let mut source = node[s].lock().unwrap();
		source.h = n as i32;
	}
	
	log!("initial pushes");
	for &i in iter {
		let mut source = node[s].lock().unwrap();
		let mut e = edge[i].lock().unwrap();
		let mut other = node[other(&source, &e)].lock().unwrap();

		log!("pushing edge {} -> {}", source.i, other.i);
		
		source.e -= e.c;
		other.e += e.c;
		e.f = e.c;
		//add other to excess list
		if other.i != t {
			excess.push_back(other.i);
		}
	}

	log!("source excess: {}", node[s].lock().unwrap().e);

	while !excess.is_empty() {
		let mut pushed = false;

		let u_i = *excess.front().unwrap();
		let mut u = node[u_i].lock().unwrap();
		let iter = adj[u_i].iter();
		
		if u.h == 0 {
			relabel(&mut u);
		}

		for &e in iter {
			if u.e == 0 {
				break;
			}

			let mut e = edge[e].lock().unwrap();
			let mut v = node[other(&u, &e)].lock().unwrap();
			
			if try_push(&mut e, &mut u, &mut v) && v.i != s && v.i != t {
				excess.push_back(v.i);
				pushed = true;
			}
		}

		if u.e > 0 {
			if !pushed {
				relabel(&mut u);
			}
			continue;
		}

		excess.pop_front();
	}

	let f = node[t].lock().unwrap().e;

	println!("f = {}", f);

}
