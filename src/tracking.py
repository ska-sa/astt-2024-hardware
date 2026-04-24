import math
from datetime import datetime

# GPS
LAT: float = -33.94462
LON: float = 18.47922

# Source
# RA 01h 42m 05s -> Convert to decimal hours first
#RA: float = 10.0
RA: float = 2 * (15.0 / 1.0) + 6 * (15.0 / 60.0) + 24 * ( 15.0 / 3600.0)
#DEC: float = 153.0
DEC: float = 12 + 47 / 60 + 52 / 3600 # Fixed the 60/3600 error in your snippet

# Datetime
NOW: datetime = datetime(2026, 4, 24, 18 - 2, 12, 0)

def compute_elevation_angle(lat: float=LAT, lon: float=LON, ra: float=RA, dec: float=DEC, now: datetime=NOW) -> float:
    # 1. Calculate Greenwich Mean Sidereal Time (GMST)
    # Using a more precise JD calculation for noon
    jd = now.toordinal() + 1721424.5 + (now.hour / 24) + (now.minute / 1440) + (now.second / 86400)
    d = jd - 2451545.0
    
    gmst = (18.697374558 + 24.06570982441908 * d) % 24
    gmst_deg = gmst * 15.0

    # 2. Calculate Local Sidereal Time (LST)
    lst = (gmst_deg + lon) % 360

    # 3. Calculate Local Hour Angle (HA)
    ha = (lst - ra) % 360
    
    # Convert to radians for math functions
    rad_lat = math.radians(lat)
    rad_dec = math.radians(dec)
    rad_ha = math.radians(ha)

    # 4. Calculate Elevation Angle (el)
    sin_el = math.sin(rad_dec) * math.sin(rad_lat) + math.cos(rad_dec) * math.cos(rad_lat) * math.cos(rad_ha)
    el = math.asin(sin_el)
    
    return math.degrees(el)

def compute_azimuthm_angle(lat: float=LAT, lon: float=LON, ra: float=RA, dec: float=DEC, now: datetime=NOW) -> float:
    el_deg = compute_elevation_angle(lat, lon, ra, dec, now)
    
    # Need HA again for the direction check
    jd = now.toordinal() + 1721424.5 + (now.hour / 24) + (now.minute / 1440) + (now.second / 86400)
    d = jd - 2451545.0
    lst = ((18.697374558 + 24.06570982441908 * d) * 15.0 + lon) % 360
    ha = (lst - ra) % 360

    rad_lat = math.radians(lat)
    rad_dec = math.radians(dec)
    rad_el = math.radians(el_deg)
    rad_ha = math.radians(ha)

    # 5. Calculate Azimuth Angle (az)
    try:
        cos_az = (math.sin(rad_dec) - math.sin(rad_el) * math.sin(rad_lat)) / (math.cos(rad_el) * math.cos(rad_lat))
        # Clamp value to avoid math domain errors due to precision
        cos_az = max(-1, min(1, cos_az))
        az = math.degrees(math.acos(cos_az))
        
        # Adjust Azimuth based on Hour Angle (East vs West)
        if math.sin(rad_ha) > 0:
            az = 360 - az
    except ZeroDivisionError:
        az = 0.0

    return az

print(f"Azimuth Angle: {compute_azimuthm_angle():.4f}°")
print(f"Elevation Angle: {compute_elevation_angle():.4f}°")
