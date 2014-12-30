package RTA;

import java.util.ArrayList;

import model.Experiment;
import model.Task;

public class ResponseTimeAnalyzer {

	private int cpusForAccesses;
	private double criticalSectionLenght;
	private Double blockingTime;
	
	ArrayList<ArrayList<Double>> weights;
	
	
	
	private void forEachCpu(ArrayList<Task> tasks) {

		ArrayList<Task> tasks_to_analyze = new ArrayList<Task>();

		// CREATE A SORT COPY OF THE ARRAYLIST
		
		int position = 0;
		for(int i = 0; i < tasks.size(); i++) {

			for(int j = 0; j < tasks_to_analyze.size(); j++) {
				if(tasks_to_analyze.get(j).getPriority() < tasks.get(i).getPriority()) {
					position++;
				}
			}

			tasks_to_analyze.add(position, tasks.get(i));

			position = 0;
		}

		for (int j = 0; j < tasks_to_analyze.size(); j++) {
			System.out.println("   = " + tasks_to_analyze.get(j).toString());
		}
		
		
		// CALCULATE WHO SUFFER BLOCK
		System.out.print("Who are suffering block?");
		
		int firstIndex = -1;
		int lastIndex = -1;

		for(int j = 0; j < tasks_to_analyze.size(); j ++) {
			if(tasks_to_analyze.get(j).isAccessResource()) {
				
				if(firstIndex == -1) {
					firstIndex = j;
					lastIndex = j;
				} else {
					lastIndex = j;
				}
			}
		}
		
		System.out.println("First " + firstIndex + " last " + lastIndex);

		// Array list dei pesi
		ArrayList<Double> arrayW = new ArrayList<>();

		for(int i = 0; i < tasks_to_analyze.size(); i++) {

			System.out.println("Task #" + tasks_to_analyze.get(i).task_id);

			Double startValue = 0.0;
			
			// Execution time
			startValue = (double) tasks_to_analyze.get(i).getExecutionTime();
			tasks_to_analyze.get(i).C = startValue;
			
			// Critical section
			if(tasks_to_analyze.get(i).isAccessResource()) {
				// La sezione critica del task e' gia' compresa nell'execution time
				startValue += blockingTime - criticalSectionLenght; 
				tasks_to_analyze.get(i).C = startValue;
			}
			
			// Blocking time suffered
			if(i > firstIndex && i < lastIndex) {
				startValue += blockingTime;
			}
			
			arrayW.add(startValue);

			// Add Interferences
			if(i > 0) {

				Double newValue = 0.0;

				Double ceiling = 0.0;

				boolean fail = false;
				boolean success = false;

				System.out.println("Partiamo con il peso uguale allo start value " + startValue);

				while(fail == success) {

					newValue = startValue;

					for(int j = i - 1; j > -1; j--) {

						System.out.println("==== Task #" + tasks_to_analyze.get(j).task_id);

						// (numerator + denominator-1) / denominator
						ceiling = (arrayW.get(i) + tasks_to_analyze.get(j).getPeriod() - 1) / tasks_to_analyze.get(j).getPeriod();
						newValue += ceiling * tasks_to_analyze.get(j).C;

						System.out.println("newValue " + newValue + "(period " + tasks_to_analyze.get(i).getPeriod() + ") ceiling " + ceiling + 
								" (" + arrayW.get(i) + "/" + tasks_to_analyze.get(j).getPeriod() + ") * " + tasks_to_analyze.get(j).C);
					}

					if(newValue == arrayW.get(i)) {
						success = true;
					} else if(newValue > tasks_to_analyze.get(i).getPeriod()) {
						fail = true;
					}

					arrayW.set(i, newValue);
					System.out.println();
				}
				
				if(fail) {
					tasks_to_analyze.get(i).RTA = false;
				} else {
					tasks_to_analyze.get(i).RTA = true;
				}
				
				System.out.println("task " + tasks_to_analyze.get(i).toString() + " RTA success " + (success?true:false));
			} else {
				tasks_to_analyze.get(i).RTA = arrayW.get(0) < tasks_to_analyze.get(i).getPeriod();				
			}
			
			System.out.println("-------- " + arrayW.toString());
			
			System.out.println("////////////////////////////");
		}
		
		weights.add(arrayW);
		
		System.out.println();
		System.out.println();

	}

	private void computeParallelAccesses (Experiment exp) {
		cpusForAccesses = 0;

		for(int i = 0; i < exp.cpus.length; i++) {
			boolean found = false;
			
			for(int j = 0; j < exp.cpus[i].tasks.size() && !found; j ++) {
				if(exp.cpus[i].tasks.get(j).isAccessResource()) {
					
					if(criticalSectionLenght == 0)
						criticalSectionLenght = exp.cpus[i].tasks.get(j).getCriticalSectionLenght();
					found = true;
				}
			}
			cpusForAccesses += (found?1:0);
			found = false;
		}
		
		if(cpusForAccesses == 0) {
			blockingTime = 0.0; 
		} else {
			blockingTime = (cpusForAccesses*criticalSectionLenght);
		}
	}

	public void computeResponseTimeAnalisis (Experiment exp) {

		criticalSectionLenght = 0;
		computeParallelAccesses(exp);
		
		weights = new ArrayList<>();

		System.out.println("Parallel accesses #" + cpusForAccesses + ". CriticalSectionLenght " + criticalSectionLenght + ". Blocking time " + blockingTime);

		for(int i = 0; i < exp.cpus.length; i++) {

			System.out.println("--- CPU #" + i);
			forEachCpu(exp.cpus[i].tasks);
		}
		
		for(int i = 0; i < weights.size(); i++) {
			System.out.println("******   " + weights.get(i).toString());
		}

	}

}
