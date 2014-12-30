package Deadline;

import java.util.ArrayList;

import model.Experiment;
import model.Record;
import model.Task;

public class DeadlineMiss {

	public ArrayList<Record> records;

	public void analyze(Experiment exp) {

		int offset = 1000000000;
		for (int i = 0; i < exp.cpus.length; i++) {
			for(int j = 0; j < exp.cpus[i].tasks.size(); j++) {
				if(exp.cpus[i].tasks.get(j).task_id < offset) {
					offset = exp.cpus[i].tasks.get(j).task_id;
				}
			}
		}

		System.out.println("offset " + offset);

		int task_id;

		for(int i = 0; i < records.size(); i++) {

			task_id = records.get(i).task - offset;

			Boolean added = false;

			for(int c = 0; c < exp.cpus.length  && !added; c++) {

				if(task_id < exp.cpus[c].tasks.size()) {
					exp.cpus[c].tasks.get(task_id).records.add(records.get(i));
					added = true;
				} else {
					if(exp.cpus[c].tasks.size() > 0) {
						task_id -= exp.cpus[c].tasks.size();
					}
				}
			}

			for(int c = 0; c < exp.cpus.length; c++) {
				for(int t = 0; t < exp.cpus[c].tasks.size(); t++) {
					int id = exp.cpus[c].tasks.get(t).task_id;
					for(int r = 0; r < exp.cpus[c].tasks.get(t).records.size(); r++) {
						if(exp.cpus[c].tasks.get(t).records.get(r).task != id) {
							System.out.println("[**] ERROR!!!");
							return;
						}
					}
				}
			}
		}
		Task task;

		System.out.println("[INIT ANALISYS]");

		int deadlinemiss = 0;

		ArrayList<String> dm = new ArrayList<String>();

		for(int c = 0; c < exp.cpus.length; c ++) {
			for(int t = 0; t < exp.cpus[c].tasks.size(); t++) { 

				task = exp.cpus[c].tasks.get(t);

				String str = "";
				String prev = "";

				for(int rc = 0; rc < task.records.size(); rc ++) {

					str = task.records.get(rc).event;

					if(str.equals(Record.k_type_completion) && prev.equals(Record.k_type_completion)) {
						task.records.get(rc).description = "Due completion di fila"; 
						task.errors.add(task.records.get(rc));
						deadlinemiss++;
						dm.add(task.records.get(rc).task + " " + task.records.get(rc).job);
					} else if(str.equals(Record.k_type_release) && prev.equals(Record.k_type_release)) {
						task.records.get(rc).description = "Due release di fila";
						task.errors.add(task.records.get(rc));
					}

					prev = str;
				}
			}
		}

		exp.print();

		System.out.println("[**] #deadline miss " + deadlinemiss);

		for(int i = 0; i < dm.size(); i++) {
			System.out.println("-- [**] " + dm.get(i));
		}

		for (int i = 0; i < exp.cpus.length; i++) {
			System.out.println("  === CPU #" + i + " ===");

			for (int j = 0; j < exp.cpus[i].tasks.size(); j++) {
				System.out.println("   = " + exp.cpus[i].tasks.get(j).toString());
				if(exp.cpus[i].tasks.get(j).errors.size() > 0) {
					for(int e = 0; e < exp.cpus[i].tasks.get(j).errors.size(); e++) {
						System.out.print(exp.cpus[i].tasks.get(j).errors.get(e).job + " - ");
					}
					System.out.println();
				}
			}
		}
	}

	public void analyzeLazy(Experiment exp) {

		for(int c = 0; c < exp.allTasks.size(); c++) {
			int id = exp.allTasks.get(c).task_id;
			for(int t = 0; t < exp.allTasks.get(c).records.size(); t++) {
				if( exp.allTasks.get(c).records.get(t).task != id) {
					System.out.println("[**] ERROR!!!");
					return;
				}
			}
		}

		Task task;

		System.out.println("[INIT ANALISYS]");

		int deadlinemiss = 0;

		ArrayList<String> dm = new ArrayList<String>();

		for(int c = 0; c < exp.allTasks.size(); c++){

			task = exp.allTasks.get(c);

			String str = "";
			String prev = "";

			int misses = 0;

			for(int rc = 0; rc < task.records.size() && misses == 0; rc ++) {

				str = task.records.get(rc).event;

				if(str.equals(Record.k_type_completion) && prev.equals(Record.k_type_completion)) {
					task.records.get(rc).description = "Due completion di fila"; 
					task.errors.add(task.records.get(rc));
					misses++;
					System.out.println(" rc " + rc + " - " + task.records.get(rc).task + " " + task.records.get(rc).job);
				} else if(str.equals(Record.k_type_release) && prev.equals(Record.k_type_release)) {
					task.records.get(rc).description = "Due release di fila";
					task.errors.add(task.records.get(rc));
					misses++;
					System.out.println(" rc " + rc + " - " + task.records.get(rc).task + " " + task.records.get(rc).job);
					//dm.add(task.records.get(rc).task + " " + task.records.get(rc).job);
				}

				prev = str;
			}

			if(misses > 0) {

				misses = 0;
				
				System.out.println("MISS DETECTOR " + task.toSched());

				for(int rc = 0; rc < task.records.size(); rc ++) {

					str = task.records.get(rc).event;

					if(str.equals(Record.k_type_release) && rc > 0) {

						String tmp = "";
						int job = task.records.get(rc).job;
						boolean found = false;

						// Ho trovato la prossima release??
						for(int i = rc - 1; i < task.records.size() && !found ; i++) {

							tmp = task.records.get(i).event;

							if(tmp.equals(Record.k_type_release)) {
								found = true;
							}

							if(tmp.equals(Record.k_type_completion)) {
								if(task.records.get(i).job != job) {
									dm.add(task.records.get(i).task + " " + task.records.get(i).job);
									misses++;
								}
							}
						}

					}
				}

			}

			deadlinemiss+=misses;

		}

		System.out.println("[**] #deadline miss " + deadlinemiss);

		for(int i = 0; i < dm.size(); i++) {
			System.out.println("-- [**] " + dm.get(i));
		}
	}
}
