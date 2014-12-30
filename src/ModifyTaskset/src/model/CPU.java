package model;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Random;
import model.Task;

public class CPU {

	public ArrayList<Task> tasks;
	public int numOfTasks;
	
	public CPU() {
		this.tasks = new ArrayList<Task>();		
		numOfTasks = 0;
	}

	public void applyRateMonotonicToCpu() {
		int lastPriotity = 10;

		for(int j = 0; j < this.tasks.size(); j ++) {
			tasks.get(j).setPriority(lastPriotity);
			lastPriotity += 10;
		}
	}

	public void add(Task task) {
		
		boolean added = false;
		
		System.out.println("add");
		
		if(tasks.size() == 0) {
			tasks.add(task);
			added = true;
			System.out.println("1");
		}
		
		for(int i = 0; i < tasks.size() && !added; i++) {
			if(tasks.get(i).getPeriod() > task.getPeriod()) {
				System.out.println("2");
				tasks.add(i, task);
				added = true;
			}
		}
		
		if(!added) {
			System.out.println("3");
			tasks.add(task);
		}
		
		numOfTasks++;
	}

	public void addResourceAccesses(int percentage) {

		int numOfTasksAccessResource = numOfTasks * percentage / 100;

		System.out.println("numOfTasksAccessResource (" + percentage + "% on " + numOfTasks + ") " + numOfTasksAccessResource);

		ArrayList<Integer> shuffle = new ArrayList<>();

		for(int i = 0; i < numOfTasks; i++) {
			if(i < numOfTasksAccessResource)
				shuffle.add(1);
			else
				shuffle.add(0);
		}

		long seed = System.nanoTime();
		Collections.shuffle(shuffle, new Random(seed));
		
		if(shuffle.size() > 0) {
			shuffle.set(0, 0);
			shuffle.set(shuffle.size() - 1, 0);
		}

		int t = 0;
		for(int i = 0; i < tasks.size(); i++) {
			if(shuffle.get(t) == 1) {
				tasks.get(i).addResourceAccess();
				System.out.println("access : " + tasks.get(i).getPartition() + " " +tasks.get(i).getPriority());
			}
			t++;
		}
	}
}
