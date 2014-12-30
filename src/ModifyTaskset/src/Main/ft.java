package Main;

import java.io.File;

import FileReader.ft_reader;

public class ft {

	/**
	 * @param args
	 */
	public static void main(String[] args) {
		new ft_reader().readFT(new File(args[0]));

	}

}
