import math

def angle_C(ac1, ac2, ab):
    """
    Estimate angle C (at vertex C) in degrees given distances:
    ac1 = distance from A to C1
    ac2 = distance from B to C2
    ab  = distance between A and B
    Assumes small equal offset x between C1/C2 and C
    """
    x = 0.3
    ac1 += x
    ac2 += x

    # Law of cosines approximation:
    # angle_C = arccos((ac1**2 + ac2**2 - ab**2) / (2 * ac1 * ac2))
    # This treats C1 and C2 as approximating C, ignoring small x
    numerator = ac1**2 + ac2**2 - ab**2
    denominator = 2 * ac1 * ac2

    # Safety check
    cos_C = max(-1.0, min(1.0, numerator / denominator))
    angle_rad = math.acos(cos_C)
    angle_deg = math.degrees(angle_rad)

    return angle_deg

# Example values from the figure
ab = 0.40
ac1 = 0.95
ac2 = 0.62

C_angle = angle_C(ac1, ac2, ab)
print(f"Estimated angle C: {C_angle:.2f} degrees")
