package Generator;

import java.util.ArrayList;

import model.Experiment;
import model.Task;

public class RTA_assignment {

	private int cpusForAccesses;
	private double criticalSectionLenght;
	private Double blockingTime;

	ArrayList<ArrayList<Double>> weights;

	private boolean forEachCpu(ArrayList<Task> tasks) {		

		// CALCULATE WHO SUFFER BLOCK
		System.out.print("Who are suffering block?");

		int firstIndex = -1;
		int lastIndex = -1;

		for(int j = 0; j < tasks.size(); j ++) {
			if(tasks.get(j).isAccessResource()) {

				if(firstIndex == -1) {
					firstIndex = j;
					lastIndex = j;
				} else {
					lastIndex = j;
				}
			}
		}

		System.out.println("First " + firstIndex + " last " + lastIndex);

		ArrayList<Double> arrayW = new ArrayList<>();

		for(int i = 0; i < tasks.size(); i++) {

			System.out.println("Task #" + tasks.get(i).task_id);

			Double startValue = 0.0;

			// Execution time
			startValue = (double) tasks.get(i).getExecutionTime();
			tasks.get(i).C = startValue;

			// Critical section
			if(tasks.get(i).isAccessResource()) {
				// La sezione critica del task e' gia' compresa nell'execution time
				startValue += blockingTime - criticalSectionLenght; 
				tasks.get(i).C = startValue;
			}

			// Blocking time suffered
			if(i > firstIndex && i < lastIndex) {
				startValue += blockingTime;
			}

			arrayW.add(startValue);

			if(i > 0) {

				double newValue = 0.0;

				double ceiling = 0.0;

				boolean fail = false;
				boolean success = false;
				
				while(fail == success) {
					newValue = startValue;
					
//					System.out.println("newValue " + newValue);

					for(int j = i - 1; j > -1; j--) {
						// (numerator + denominator-1) / denominator
						ceiling = Math.ceil(arrayW.get(i) / tasks.get(j).getPeriod());
//						System.out.println("ceiling " + ceiling);
						newValue += ceiling * tasks.get(j).C;
					}
					
					newValue = ((double)((int) (newValue * 100))) / 100;
					
					if(newValue == arrayW.get(i)) {
						if(i > 0) 
							if(newValue < (arrayW.get(i-1)))
								newValue = (arrayW.get(i-1)) + 1;
						success = true;
					} else if(newValue > 300.0) {
						System.out.println("/********************************/");
						System.out.println("            " + newValue);
						System.out.println("/********************************/");
						return false;
					}
					
					arrayW.set(i, newValue);
					
//					System.out.println("arrayW.get(i) " + arrayW.get(i));
				}
				
				tasks.get(i).setPeriod(arrayW.get(i));
				tasks.get(i).RTA = true;
			} else {
				arrayW.set(i, tasks.get(i).getPeriod());
				tasks.get(i).RTA = arrayW.get(0) < tasks.get(i).getPeriod();				
			}
		}

		weights.add(arrayW);
		return true;
	}

	private void computeParallelAccesses (Experiment exp) {
		cpusForAccesses = 0;

		for(int i = 0; i < exp.cpus.length; i++) {
			boolean found = false;

			for(int j = 0; j < exp.cpus[i].tasks.size() && !found; j ++) {
				if(exp.cpus[i].tasks.get(j).isAccessResource()) {
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

	public boolean addDeadlines (Experiment exp) {
		
		criticalSectionLenght = exp.criticalSectionLenght;
		computeParallelAccesses(exp);
	
		boolean success = true;

		weights = new ArrayList<>();

		for(int i = 0; i < exp.cpus.length && success; i++) {
			success = forEachCpu(exp.cpus[i].tasks);
		}

		for(int i = 0; i < weights.size(); i++) {
			System.out.println("******   " + weights.get(i).toString());
		}

		return success;
	}
}