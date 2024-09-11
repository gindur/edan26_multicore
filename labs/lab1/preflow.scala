import scala.util._
import java.util.Scanner
import java.io._
import akka.actor._
import akka.pattern.ask
import akka.util.Timeout
import scala.concurrent.{Await,ExecutionContext,Future,Promise}
import scala.concurrent.duration._
import scala.language.postfixOps
import scala.io._
import scala.util.{Success, Failure}

case class Flow(f: Int)
case class Debug(debug: Boolean)
case class Control(control:ActorRef)
case class Source(n: Int)

case class Push(a: Edge, h: Int, df: Int)
case class Rollback(a: Edge, df: Int)

case object Print
case object Start
case object Excess
case object Excess2
case object Maxflow
case object Sink
case object Hello

case object Discharge

class Edge(var u: ActorRef, var v: ActorRef, var c: Int) {
	var	f = 0
}

class Node(val index: Int) extends Actor {
	var	e = 0;                          // excess preflow
	var	h = 0;                          // height.
	var	control:ActorRef = null	        /* controller to report to when e is zero. 			*/
	var	source:Boolean	= false		/* true if we are the source.					*/
	var	sink:Boolean	= false		/* true if we are the sink.					*/
	var	edge: List[Edge] = Nil		/* adjacency list with edge objects shared with other nodes.	*/
	var	debug = true			/* to enable printing.						*/

    var pushes = 0
	
	def min(a:Int, b:Int) : Int = { if (a < b) a else b }

	def id: String = "@" + index;

	def other(a:Edge, u:ActorRef) : ActorRef = { if (u == a.u) a.v else a.u }

	def status: Unit = { if (debug) println(id + " e = " + e + ", h = " + h) }

	def enter(func: String): Unit = { if (debug) { println(id + " enters " + func); status } }
	def exit(func: String): Unit = { if (debug) { println(id + " exits " + func); status } }

	def relabel : Unit = {
		enter("relabel")
		h += 1
		exit("relabel")
	}


	def receive = {

	case Debug(debug: Boolean)	=> this.debug = debug

	case Print => status

	case Excess => { sender ! Flow(e) /* send our current excess preflow to actor that asked for it. */ }

    case Excess2 => { sender ! e }

	case edge:Edge => { this.edge = edge :: this.edge /* put this edge first in the adjacency-list. */ }

	case Control(control:ActorRef) => this.control = control

	case Sink => { sink = true }

	case Source(n:Int)	=> {
		h = n
		source = true

        for (a <- edge) {
            val other_node = other(a, self)

            a.f = a.c
            this.e -= a.c

            other_node ! Push(a, this.h, a.c)
        }
	}

    case Discharge => {
        enter("discharge")
        var p: List[Edge] = Nil   // pointer into edge
        var a: Edge = null        // edge to work with
        p = edge

        while (p != Nil) {
            a = p.head
            p = p.tail

            if (e > 0 && a.f < a.c) {
                val other_node = other(a, self)

                pushes += 1

                val df = min(e, a.c - a.f)

                // Change local flow
                a.f += df
                this.e -= df

                // Change other flow
                other_node ! Push(a, this.h, df)
            }
        }
        exit("discharge")
    }

    case Push(a:Edge, h:Int, df:Int) => {
        enter("push")
        if (h > this.h)
            this.e += df
        else
            sender ! Rollback(a, df)
        exit("push")
    }

    case Rollback(a:Edge, df:Int) => {
        enter("rollback")
        a.f -= df
        this.e += df

        pushes -= 1

        if (pushes == 0)
            relabel

        exit("rollback")
    }

	case _		=> {
		println("" + index + " received an unknown message" + _) }
		assert(false)
	}
}


class Preflow extends Actor {
	var	s = 0;			            // index of source node.
	var	t = 0;			            // index of sink node.
	var	n = 0;			            // number of vertices in the graph.
	var	edge:Array[Edge] = null	    // edges in the graph.
	var	node:Array[ActorRef] = null	// vertices in the graph.
	var	ret:ActorRef = null	        // Actor to send result to.					*/

	def receive = {
	    case node:Array[ActorRef]	=> {
	    	this.node = node
	    	n = node.size
	    	s = 0
	    	t = n-1
	    	for (u <- node)
	    		u ! Control(self)
	    }

	    case edge:Array[Edge] => this.edge = edge

	    case Flow(f:Int) => {
	    	  ret ! f			/* somebody (hopefully the sink) told us its current excess preflow. */
	    }

	    case Maxflow => {
            println("Maxflow start")
	        ret = sender
            node(s) ! Source(n)
            node(t) ! Sink
	    	// node(t) ! Excess	/* ask sink for its excess preflow (which certainly still is zero). */

            var sf = 0
            var tf = 0
            
	        implicit val time = Timeout(1 seconds);
            println("Discharge loop start")
            do {
                for (u <- node) {
                    if (u != node(s))
                        u ! Discharge
                }

                implicit val ec = ExecutionContext.global

                val source_flow = node(s) ? Excess2
				sf = Await.result(source_flow, time.duration) match {
				case i: Int => i
				}

				val sink_flow = node(t) ? Excess2
				tf = Await.result(sink_flow, time.duration) match {
				case i: Int => i
}

            } while (sf.abs != tf)

            // sender ! (node(t) ? Excess)
            node(t) ! Excess
	    }
    }
}

object main extends App {
	implicit val t = Timeout(4 seconds);

    println("Preflow Push Algorithm")

	val	begin = System.currentTimeMillis()
	val system = ActorSystem("Main")
	val control = system.actorOf(Props[Preflow], name = "control")

	var	n = 0;
	var	m = 0;
	var	edge: Array[Edge] = null
	var	node: Array[ActorRef] = null

	val	s = new Scanner(System.in);

	n = s.nextInt
	m = s.nextInt

	/* next ignore c and p from 6railwayplanning */
	s.nextInt
	s.nextInt

	node = new Array[ActorRef](n)

	for (i <- 0 to n-1)
		node(i) = system.actorOf(Props(new Node(i)), name = "v" + i)

	edge = new Array[Edge](m)

	for (i <- 0 to m-1) {
		val u = s.nextInt
		val v = s.nextInt
		val c = s.nextInt

		edge(i) = new Edge(node(u), node(v), c)

		node(u) ! edge(i)
		node(v) ! edge(i)
	}

	control ! node
	control ! edge
 
    println("Start")
	val flow = control ? Maxflow
	val f = Await.result(flow, t.duration)
    println("End")

	system.stop(control);
    for (i <- 0 to n-1)
        system.stop(node(i))

	system.terminate()

	val	end = System.currentTimeMillis()

    println("f = " + f)
	println("t = " + (end - begin) / 1000.0 + " s")
}
