package model;

import java.util.ArrayList;

public class Experiment {

	public CPU[] cpus;
	public int numOfCpus;
	
	public ArrayList<Task> allTasks;
	
	
	public double criticalSectionLenght;

	public Experiment(int _cpus) {

		this.numOfCpus = _cpus;
		
		this.allTasks = new ArrayList<Task>();

		this.cpus = new CPU[numOfCpus];

		for(int i = 0; i < _cpus; i++) {
			this.cpus[i] = new CPU();
		}		
	}
	
	public void print() {
		
		System.out.println("=== Print experiment ===");

		for (int i = 0; i < cpus.length; i++) {
			System.out.println("  === CPU #" + i + " ===");

			for (int j = 0; j < cpus[i].tasks.size(); j++) {
				System.out.println("   = " + cpus[i].tasks.get(j).toString());
			}
		}

		System.out.println("========================");
	}

	public void computeCriticalSectionLenght() {

		Double min = (double) Integer.MAX_VALUE;

		for(int i = 0; i < cpus.length; i ++) {
			for(int j = 0; j < cpus[i].tasks.size(); j++) {
				if(cpus[i].tasks.get(j).getExecutionTime() < min) {
					min = (double) cpus[i].tasks.get(j).getExecutionTime();
				}
			}
		}

		criticalSectionLenght = min / 2;

		System.out.println("criticalSectionLenght " + criticalSectionLenght);

		int count = 0;
		
		for(int c = 0; c < cpus.length; c ++) {
			for(int j = 0; j < cpus[c].tasks.size(); j++) {
				if(cpus[c].tasks.get(j).isAccessResource()) {
					count++;
					cpus[c].tasks.get(j).setCriticalSectionLenght(criticalSectionLenght);
				}
			}
		}
		
		System.out.println("== " + count);
	}
}
