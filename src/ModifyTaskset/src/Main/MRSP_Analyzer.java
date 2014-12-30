package Main;

import java.io.File;
import java.io.IOException;

import Deadline.DeadlineMiss;
import FileReader.SchedReader;
import FileReader.UnitTraceReader;
import RTA.ResponseTimeAnalyzer;

import model.Experiment;

public class MRSP_Analyzer {
	
	private static boolean lazy = true;
	private static boolean analyze = false;
	private static boolean ratemonotonic = false;
	

	public static void main(String[] args) {
		
		if(lazy) {
			Experiment exp = new Experiment(4);
			
			String path;
			if(args.length > 0) {
				path = args[0];
			} else {
				path = "traces";
			}
			
			File traces = new File(path);
			
			System.out.println("[READ UNIT-TRACES]");
			
			UnitTraceReader traceReader = new UnitTraceReader();

			try {
				traceReader.readTracesLazy(traces, exp);
			} catch (IOException e) {
				e.printStackTrace();
			}
			
			exp.print();
			System.out.println();
			
			System.out.println("[DEADLINE MISSES ANALYSIS]");
			
			DeadlineMiss deadlineAnalyzer = new DeadlineMiss();
						
			deadlineAnalyzer.analyzeLazy(exp);
		} else if(analyze){
			Experiment exp = new Experiment(4);
			
			File sched = new File("sched.py");
			File traces = new File("traces");
			
			System.out.println("[READ SCHED.PY]");
			
			SchedReader schedReader = new SchedReader();
			
			try {
				schedReader.readTasksetFromFullSched(sched, exp);
			} catch (IOException e) {
				e.printStackTrace();
			}
			
			//exp.print();
			System.out.println();
			
			System.out.println("[READ UNIT-TRACES]");
			
			UnitTraceReader traceReader = new UnitTraceReader();

			try {
				traceReader.readTraces(traces, exp);
			} catch (IOException e) {
				e.printStackTrace();
			}
			
			exp.print();
			System.out.println();
			
			System.out.println("[COMPUTE RESPONSE TIME ANALYSIS]");
			
			ResponseTimeAnalyzer analyzer = new ResponseTimeAnalyzer();
			
			analyzer.computeResponseTimeAnalisis(exp);
			
			exp.print();
			System.out.println();
			
			System.out.println("[DEADLINE MISSES ANALYSIS]");
			
			DeadlineMiss deadlineAnalyzer = new DeadlineMiss();
			
			deadlineAnalyzer.records = traceReader.records;
			
			deadlineAnalyzer.analyze(exp);
		} else if(ratemonotonic){
			
			String path;
			
			if(args.length > 0) {
				path = args[0];
			} else {
				path = "sched.py";
			}
			
			Experiment exp = new Experiment(4);
			
			File sched = new File(path);
			
			System.out.println("[READ SCHED.PY]");
			
			SchedReader schedReader = new SchedReader();
			
			try {
				schedReader.readTasksetFromFullSched(sched, exp);
			} catch (IOException e) {
				e.printStackTrace();
			}
			
			for(int i = 0; i < exp.cpus.length; i++) {
				exp.cpus[i].applyRateMonotonicToCpu();
			}
			
			try {
				schedReader.writeTaskset(sched, exp);
			} catch (IOException e) {
				e.printStackTrace();
			}
			
			exp.print();
		}
		
		

		/*
		try {
			exp.taskset = schedReader.ReadTasksetSimpleForRTA(file, 4);
		} catch (IOException e) {
			e.printStackTrace();
		}
		
		*/
		
		
	}

	/*public static void mainTmp(String[] args) {
		
		int numOfCpus = 4;
		int percentageTaskAccessResource = 35;
		
		Experiment exp = new Experiment();
		
		File file = new File("test.txt");
		
		SchedReader schedReader = new SchedReader();
		
		try {
			exp.taskset = schedReader.readTaskset(file, numOfCpus);
		} catch (IOException e) {
			e.printStackTrace();
		}
		
		// Rate Monotonic Priority Assigment
		exp.taskset.applyRateMonotonic();	
		
		// Random choose of Tasks Access Resource
		exp.taskset.addResourceAccesses(percentageTaskAccessResource);
		
		try {
			schedReader.writeTaskset(file, exp.taskset);
		} catch (IOException e) {
			e.printStackTrace();
		}
		
		ResponseTimeAnalyzer analyzer = new ResponseTimeAnalyzer();
		
		analyzer.computeResponseTimeAnalisy(exp);

	}*/

}
