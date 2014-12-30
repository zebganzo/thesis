package model;

import java.util.ArrayList;

public class Task {
	
	public int task_id;
	public boolean RTA;
	
// -p 2 -q 4 -X MRSP -L 2 -Q 1 4 11
	private int partition;
	private int priority;
	
	private boolean accessResource;
	private String protocol;
	private int resourceId;
	
	private double criticalSectionLenght;
	
	private int executionTime;
	private Double period;
	
	public Double C;
	
	public ArrayList<Record> task_s_record;
	
	public ArrayList<Record> errors;
	public ArrayList<Record> records;
	
	public Task(int _partition, int _executionTime, double _period) {
		setPartition(_partition);
		setExecutionTime(_executionTime);
		setPeriod(_period);
		
		setPriority(-1);
		setAccessResource(false);
		setCriticalSectionLenght(-1);
		setProtocol(null);
		setResourceId(-1);
		RTA = false;
		errors = new ArrayList<Record>();
		records = new ArrayList<Record>();
	}
	
	public Task(int _partition, int _executionTime, double _period, int priority) {
		setPartition(_partition);
		setExecutionTime(_executionTime);
		setPeriod(_period);
		
		setPriority(priority);
		setAccessResource(false);
		setCriticalSectionLenght(-1);
		setProtocol(null);
		setResourceId(-1);
		RTA = false;
		errors = new ArrayList<Record>();
		records = new ArrayList<Record>();
	}
	
	public void addResourceAccess() {
		setAccessResource(true);
		setProtocol("MRSP");
		setResourceId(1);
	}
	
	public String toString() {		
		StringBuilder description = new StringBuilder();
		
		// -p 2 -q 4 -X MRSP -L 2 -Q 1 4 11
		description.append("-p ");
		description.append(getPartition());
		
		if(getPriority() != -1) {
			description.append(" -q ");
			description.append(getPriority());
		}
		
		if(isAccessResource()) {
			description.append(" -X ");
			description.append(getProtocol());
			description.append(" -L ");
			description.append(getCriticalSectionLenght());
			description.append(" -Q ");
			description.append(getResourceId());
		}
		
		description.append(" (exec: ");
		description.append(getExecutionTime());
		description.append(") (period: ");
		description.append(getPeriod());
		
		description.append(") (task id: " + task_id +")");
		
		description.append(" RTA " + (RTA?"success":"failed"));
		description.append(" DL MISS " + (errors.size() == 0?"None":errors.size()/2));
		
		return description.toString();
	}
	
	public String toSched() {		
		StringBuilder description = new StringBuilder();
		
		// -p 2 -q 4 -X MRSP -L 2 -Q 1 4 11
		description.append("-p ");
		description.append(getPartition());
		
		if(getPriority() != -1) {
			description.append(" -q ");
			description.append(getPriority());
		}
		
		if(isAccessResource()) {
			description.append(" -X ");
			description.append(getProtocol());
			description.append(" -L ");
			description.append(getCriticalSectionLenght());
			description.append(" -Q ");
			description.append(getResourceId());
		}
		
		description.append(" ");
		description.append(getExecutionTime());
		description.append(" ");
		description.append(getPeriod());
		
		return description.toString();
	}
	
	public int getPriority() {
		return priority;
	}
	public void setPriority(int priority2) {
		this.priority = priority2;
	}
	public boolean isAccessResource() {
		return accessResource;
	}
	public void setAccessResource(boolean accessoResource) {
		this.accessResource = accessoResource;
	}
	public double getCriticalSectionLenght() {
		return criticalSectionLenght;
	}
	public void setCriticalSectionLenght(double criticalSectionLenght) {
		this.criticalSectionLenght = criticalSectionLenght;
	}
	public String getProtocol() {
		return protocol;
	}
	public void setProtocol(String protocol) {
		this.protocol = protocol;
	}
	public int getResourceId() {
		return resourceId;
	}
	public void setResourceId(int resourceId) {
		this.resourceId = resourceId;
	}
	public int getExecutionTime() {
		return executionTime;
	}
	public void setExecutionTime(int executionTime) {
		this.executionTime = executionTime;
	}
	public Double getPeriod() {
		return period;
	}
	public void setPeriod(Double _period) {
		this.period = _period;
	}
	
	public int getPartition() {
		return partition;
	}
	public void setPartition(int partition) {
		this.partition = partition;
	}
}
