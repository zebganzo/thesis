package FileReader;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.security.AllPermission;
import java.util.ArrayList;

import model.Experiment;
import model.Record;
import model.Task;

public class UnitTraceReader {

	/* Event ID: 15
	 * Job: 1778.1
	 * Type: switch_away
	 * Time: 243571753766 */
	
	// Normal records
	public ArrayList<Record> records;
	
	// Records useful to retrieve tasks' parameters
	public ArrayList<Record> tasks_record;
		
	private int readEvent(final String str) {
		return Integer.parseInt(str.replace(Record.k_event_id, ""));
	}

	private int readJob(final String str) {
		int index = str.indexOf(".");
		return Integer.parseInt(str.substring(index+1, str.length()));
	}
	
	private int readTask(String str) {
		str = str.replace(Record.k_job, "");
		int index = str.indexOf(".");
		return Integer.parseInt(str.substring(0, index));
	}

	private String readType(String str) {
		return str.replace(Record.k_type, "");
	}

	private long readTime(final String str) {
		return Long.parseLong(str.replace(Record.k_time, ""));
	}

	public void readTracesLazy(File file, Experiment exp) throws IOException {
				
		int task_id_set = 0;
		int task_id_tmp = 0;
		
		FileReader fileReader = new FileReader(file);
		BufferedReader bufferedReader = new BufferedReader(fileReader);

		Record record = new Record();
		
		records = new ArrayList<>();
		tasks_record = new ArrayList<>();
		
		String line;
		while ((line = bufferedReader.readLine()) != null) {
			if(line.startsWith(Record.k_event_id)) {
				record = new Record();
				record.id = readEvent(line);
			} else if(line.startsWith(Record.k_job)) {
				record.job = readJob(line);
				record.task = readTask(line);
			} else if(line.startsWith(Record.k_type)) {
				record.event = readType(line);
			} else if(line.startsWith(Record.k_time)) {
				record.time = readTime(line);
				
				if(record.event.equals(Record.k_type_release) || record.event.equals(Record.k_type_completion)) {					
					records.add(record);
					
					int task_id = record.task;
					boolean found = false;
					
					for(int i = 0; i < exp.allTasks.size() && !found; i++) {
						if(exp.allTasks.get(i).task_id == task_id) {
							exp.allTasks.get(i).records.add(record);
						}
					}
				}
				
				if(record.event.equals(Record.k_type_name)) {
					Task t = new Task(0, 0, 0);
					t.task_id = record.task;
					exp.allTasks.add(t);
				}
			}
 		}

		fileReader.close();
	}
	
	public void readTraces(File file, Experiment exp) throws IOException {
		
		int task_id_set = 0;
		int task_id_tmp = 0;
		
		FileReader fileReader = new FileReader(file);
		BufferedReader bufferedReader = new BufferedReader(fileReader);

		Record record = new Record();
		
		records = new ArrayList<>();
		tasks_record = new ArrayList<>();
		
		String line;
		while ((line = bufferedReader.readLine()) != null) {
			if(line.startsWith(Record.k_event_id)) {
				record = new Record();
				record.id = readEvent(line);
			} else if(line.startsWith(Record.k_job)) {
				record.job = readJob(line);
				record.task = readTask(line);
			} else if(line.startsWith(Record.k_type)) {
				record.event = readType(line);
			} else if(line.startsWith(Record.k_time)) {
				record.time = readTime(line);
				
				if(record.event.equals(Record.k_type_release) || record.event.equals(Record.k_type_completion)) {					
					records.add(record);
				}
				
				if(record.event.equals(Record.k_type_name)) {
					tasks_record.add(record);
					
					task_id_tmp = task_id_set;
					
					for(int i = 0; i < exp.cpus.length; i++) {
						
						if(task_id_tmp < exp.cpus[i].tasks.size()) {
							exp.cpus[i].tasks.get(task_id_tmp).task_id = record.task;
						} else {
							if(exp.cpus[i].tasks.size() > 0) {
								task_id_tmp -= exp.cpus[i].tasks.size();
							}
						}
					}
					
					task_id_set++;
				}
			}
 		}

		fileReader.close();
	}
}
