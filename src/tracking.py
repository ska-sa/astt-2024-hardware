import math
from datetime import datetime

def calculate_sun_az_el(lat: float, lon: float, dt_utc: datetime):
    # Constants
    RAD = math.pi / 180.0
    DEG = 180.0 / math.pi

    # 1. Time Calculations
    # Day of year (n)
    n = dt_utc.timetuple().tm_yday
    # Fractional hour of the day
    hour_utc = dt_utc.hour + dt_utc.minute / 60.0 + dt_utc.second / 3600.0

    # 2. Solar Declination (delta)
    # Range: -23.44 to +23.44 degrees
    # This represents the Sun's "latitude" relative to the equator
    delta = 23.44 * math.sin(RAD * (360 / 365.25 * (n - 81)))
    
    # 3. Equation of Time (EoT) 
    # Corrects for the Earth's elliptical orbit and axial tilt
    b = RAD * (360 / 365.25 * (n - 81))
    eot = 9.87 * math.sin(2 * b) - 7.53 * math.cos(b) - 1.5 * math.sin(b)

    # 4. Local Solar Time (LST) and Hour Angle (H)
    # Adjust UTC for longitude (15 deg/hour) and the Equation of Time
    lst = hour_utc + (lon / 15.0) + (eot / 60.0)
    # Hour angle: 0 at solar noon, -15 deg per hour before noon, +15 deg after
    h = (lst - 12.0) * 15.0

    # 5. Calculate Elevation (El)
    phi_rad = lat * RAD
    delta_rad = delta * RAD
    h_rad = h * RAD
    
    sin_el = (math.sin(phi_rad) * math.sin(delta_rad) + 
              math.cos(phi_rad) * math.cos(delta_rad) * math.cos(h_rad))
    # Clamp to handle rounding errors
    sin_el = max(-1.0, min(1.0, sin_el))
    el_rad = math.asin(sin_el)
    el_deg = el_rad * DEG

    # 6. Calculate Azimuth (Az)
    # Measured clockwise from North (0 degrees)
    cos_az = (math.sin(delta_rad) - math.sin(el_rad) * math.sin(phi_rad)) / (math.cos(el_rad) * math.cos(phi_rad))
    # Clamp to avoid math domain errors
    cos_az = max(-1.0, min(1.0, cos_az))
    az_deg = math.acos(cos_az) * DEG
    
    # Correction for afternoon (if Hour Angle is positive, Sun is in the West)
    if h > 0:
        az_deg = 360.0 - az_deg

    return az_deg, el_deg

# --- INPUTS ---
lat_input: float = -33.9411
lon_input: float = 18.491
time_input: datetime = datetime(2026, 3, 27, 12, 30, 0) # UTC Time

# --- EXECUTION ---
az, el = calculate_sun_az_el(lat_input, lon_input, time_input)

print(f"Source:    SUN")
print(f"UTC Time:  {time_input}")
print(f"Location:  Lat {lat_input}, Lon {lon_input}")
print("-" * 30)
print(f"Azimuth:   {az:.4f}° (North-based)")
print(f"Elevation: {el:.4f}°")
