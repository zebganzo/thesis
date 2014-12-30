package Generator;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Random;

import FileReader.SchedReader;

import model.Experiment;
import model.Task;

public class TasksetGenerator {

	private int numOfCpu = 4;
	private int numOfTask;
	private Random random;

	private int folders_count = 1001;

	private Experiment exp;

	private int minNumOfTask = 15;

	public void start() {

		while(folders_count < 10000) {

			boolean success = false;
			int fails = 0;

			while(!success) {

				random = new Random();

				numOfTask = minNumOfTask + random.nextInt(10);

				exp = new Experiment(numOfCpu);

				System.out.println(numOfTask);


				ArrayList<Integer> shuffle = new ArrayList<>();

				for(int i = 0; i < numOfTask; i++) {
					shuffle.add(random.nextInt(numOfCpu));
					System.out.print(shuffle.get(i) + " ");
				}

				long seed = System.nanoTime();
				Collections.shuffle(shuffle, new Random(seed));

				System.out.println();

				for(int i = 0; i < numOfTask; i++) {
					Task task  = new Task(shuffle.get(i), random.nextInt(3) + 1, random.nextInt(4) + 6);
					exp.cpus[task.getPartition()].add(task);
				}

				for(int i = 0; i < exp.cpus.length; i++) {
					int priority = 10;
					for(int j= 0; j < exp.cpus[i].tasks.size(); j++) {
						exp.cpus[i].tasks.get(j).setPriority(priority);
						priority+=10;
					}

					exp.cpus[i].addResourceAccesses(random.nextInt(40) + 40);
				}

				exp.computeCriticalSectionLenght();

				RTA_assignment rta = new RTA_assignment();

				success = rta.addDeadlines(exp);

				if(!success) {
					fails++;
				}

				exp.print();

			}

			System.out.println("fails " + fails);

			SchedReader sched = new SchedReader();

			File folder = new File("../mrsp_exps/mrsp" + folders_count);
			folders_count++;
			folder.mkdirs();

			File sched_py = new File(folder.getAbsolutePath() + File.separator + "sched.py");

			String params = "{'cpus': 4,\n 'duration': 5, \n 'periods': 'harmonic', \n 'release_master': False, \n 'scheduler': 'P-FP', \n 'utils': 'uni-medium'}";
			File params_py = new File(folder.getAbsolutePath() + File.separator + "params.py");

			try {
				sched.writeTaskset(sched_py, exp);
				sched.writeParams(params_py, params);
			} catch (IOException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
		}
	}

	public int getNumOfCpu() {
		return numOfCpu;
	}

	public void setNumOfCpu(int numOfCpu) {
		this.numOfCpu = numOfCpu;
	}

}
