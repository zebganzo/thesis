package FileReader;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;

public class ft_reader {

	public void readFT(File file) {
		
		FileReader fileReader = null;
		try {
			fileReader = new FileReader(file);
		} catch (FileNotFoundException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		BufferedReader bufferedReader = new BufferedReader(fileReader);
				
		String line;
		int count = 0;
		int sum = 0;
		
		try {
			while ((line = bufferedReader.readLine()) != null) {
				line = line.replace(" ", "");
				String values[] = line.split(",");
				
				sum += Integer.parseInt(values[2]);
				count++;
			}
		} catch (IOException e) {
			e.printStackTrace();
		}
		
		System.out.println("[***] count " + count + " avg " + (((double)sum)/((double)count)));
		
		try {
			fileReader.close();
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
	}
	
}
