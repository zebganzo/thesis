package model;

/* Event ID: 15
 * Job: 1778.1
 * Type: switch_away
 * Time: 243571753766 */

public class Record {
	
	public static final String k_event_id = "Event ID: ";
	public static final String k_job = "Job: ";
	public static final String k_type = "Type: ";
	public static final String k_time = "Time: ";
	
	public static final String k_type_switch_away = "switch_away";
	public static final String k_type_block = "block";
	public static final String k_type_resume = "resume";
	public static final String k_type_switch_to = "switch_to";
	public static final String k_type_release = "release";
	public static final String k_type_completion = "completion";
	public static final String k_type_name = "name";
	public static final String k_type_params = "params";
	
	public String description;
	
	public int id;
	public int task;
	public int job;
	public String event;
	public long time;
	
	public void print() {
		System.out.println("Event id - " + id);
		System.out.println("task     - " + task);
		System.out.println("job      - " + job);
		System.out.println("type     - " + event);
		System.out.println("time     - " + time);
	}
	
}
