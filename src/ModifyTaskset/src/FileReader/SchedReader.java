package FileReader;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;

import model.Experiment;
import model.Task;

public class SchedReader {

	public void readTasksetFromFullSched(File file, Experiment exp) throws IOException {

		FileReader fileReader = new FileReader(file);
		BufferedReader bufferedReader = new BufferedReader(fileReader);

		Task task;

		String line;
		while ((line = bufferedReader.readLine()) != null) {
			if(!line.isEmpty()) {
				task = readFullTask(line);
				if(task.getPriority() == -1) {
					exp.cpus[task.getPartition()].add(task);
				} else {
					exp.cpus[task.getPartition()].tasks.add(task);
				}
			}
		}

		fileReader.close();
	}

	private Task readFullTask(final String line) {

		System.out.println(line);

		String[] params = line.replace("  ", " ").split(" ");

		Task t = null;
		
		if(params.length == 4) {
			//-p 0  5 50
			// 	public Task(int _partition, int _executionTime, double _period, int priority) {

			t = new Task(Integer.parseInt(params[1]),
					Integer.parseInt(params[2]),
					Double.parseDouble(params[3]),
					-1);
			System.out.println("task simple " + t.toSched());
		}
		

		if(params.length == 6) {
			t = new Task(Integer.parseInt(params[1]),
					Integer.parseInt(params[4]),
					Double.parseDouble(params[5]),
					Integer.parseInt(params[3]));
		}

		// -p 2 -q 4 -X MRSP -L 2 -Q 1 4 11
		if(params.length == 12) {
			t = new Task(Integer.parseInt(params[1]),
					Integer.parseInt(params[10]),
					Double.parseDouble(params[11]),
					Integer.parseInt(params[3]));
			t.addResourceAccess();
			t.setCriticalSectionLenght(Double.parseDouble(params[7]));
		}
		return t;	
	}

	public Task readTask(final String strTask) {

		System.out.println(strTask);

		String[] params = strTask.replace("  ", " ").split(" ");

		Task t = new Task(Integer.parseInt(params[1]),
				Integer.parseInt(params[2]),
				Integer.parseInt(params[3]));
		return t;
	}

	/*public ArrayList<Task> readTasksetSimple(File file) throws IOException {

		FileReader fileReader = new FileReader(file);
		BufferedReader bufferedReader = new BufferedReader(fileReader);

		ArrayList<Task> taskList = new ArrayList<>();
		Task task;

		String line;
		while ((line = bufferedReader.readLine()) != null) {
			task = readFullTask(line);
			taskList.add(task);
		}

		fileReader.close();

		return taskList;
	}*/

	/*public TaskList ReadTasksetSimpleForRTA(File file, int numOfCpus) throws IOException {

		FileReader fileReader = new FileReader(file);
		BufferedReader bufferedReader = new BufferedReader(fileReader);

		TaskList taskList = new TaskList(numOfCpus);
		Task task;

		String line;
		while ((line = bufferedReader.readLine()) != null) {
			task = readFullTask(line);
			System.out.println("Pre add " + (task == null));
			taskList.add(task);
			System.out.println("Post add");

		}

		fileReader.close();

		return taskList;
	}*/

	public void writeTaskset(File file, Experiment exp) throws IOException {
		FileWriter fileWriter = new FileWriter(file);

		for(int i = 0; i < exp.cpus.length; i ++) {
			for(int j = 0; j < exp.cpus[i].tasks.size(); j ++) {
				fileWriter.write(exp.cpus[i].tasks.get(j).toSched() + "\n");
			}
		}

		fileWriter.flush();
		fileWriter.close();
	}

	public void writeParams(File params_py, String params) throws IOException {
		FileWriter fileWriter = new FileWriter(params_py);

		fileWriter.write(params);

		fileWriter.flush();
		fileWriter.close();
	}
}