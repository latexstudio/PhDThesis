package io.crowbar.diagnostic.spectrum.unserializers;

import static org.junit.Assert.*;
import io.crowbar.diagnostic.spectrum.Component;
import io.crowbar.diagnostic.spectrum.Spectrum;
import io.crowbar.diagnostic.spectrum.activity.Hit;

import java.util.Scanner;

import org.junit.Test;

public class HitSpectrumUnserializerTest {

	@Test
	public void testSerializer() {
		String in = "10 9 0 1 0 0 0 0 0 0 0 0 0.0 0 0 0 1 0 0 0 0 0 0 0.0 1 0 0 0 0 1 0 0 0 0 0.0 0 0 0 0 0 0 0 1 0 0 0.0 0 0 0 0 0 0 0 0 1 0 0.0 0 0 1 0 0 0 0 0 0 0 1.0 0 0 0 0 1 0 0 0 0 0 1.0 0 0 0 0 0 0 1 0 0 0 1.0 0 0 0 0 0 0 0 0 0 1 1.0";

		Spectrum s = HitSpectrumUnserializer.unserialize(new Scanner(in));
		
		//s.toString();
		
	    //TODO Assert
	}

	@Test
	public void testGetComponent() {
		String in = "10 9 0 1 0 0 0 0 0 0 0 0 0.0 0 0 0 1 0 0 0 0 0 0 0.0 1 0 0 0 0 1 0 0 0 0 0.0 0 0 0 0 0 0 0 1 0 0 0.0 0 0 0 0 0 0 0 0 1 0 0.0 0 0 1 0 0 0 0 0 0 0 1.0 0 0 0 0 1 0 0 0 0 0 1.0 0 0 0 0 0 0 1 0 0 0 1.0 0 0 0 0 0 0 0 0 0 1 1.0";

		Spectrum<Hit, ?> s = HitSpectrumUnserializer.unserialize(new Scanner(in));
				
		Component c = s.getComponent(0);
		
		assertNotEquals(c, null);
	    assertEquals(c.getNodeId(),0);
	}	
}