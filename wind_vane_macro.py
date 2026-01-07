"""
FreeCAD Macro: Wind Vane for AS5600 Magnetic Rotary Position Sensor
====================================================================

This macro creates a 3D model of a wind vane designed to work with an 
AS5600 breakout board. The AS5600 is a 12-bit magnetic rotary position 
sensor that detects the angular position of a magnet.

Components created:
- Rotating shaft with magnet holder
- Wind vane (tail fin)
- Arrow pointer (for visual direction indication)
- Base mount for AS5600 breakout board
- Bearing housing

Usage:
1. Open FreeCAD
2. Run this macro (Macro -> Macros... -> Execute)
3. Adjust parameters in the dialog if needed
4. The complete wind vane assembly will be created

Hardware requirements:
- AS5600 breakout board
- 6mm diameter x 3mm thick round neodymium magnet
- 688ZZ bearing (8mm ID, 16mm OD, 5mm width)
"""

import FreeCAD as App
import Part
import math
from FreeCAD import Vector

# === Configuration Parameters ===
# These can be adjusted based on your specific requirements

# Shaft parameters
SHAFT_DIAMETER = 8.0  # mm - should match bearing inner diameter
SHAFT_LENGTH = 60.0   # mm - total length of rotating shaft
SHAFT_BASE_LENGTH = 15.0  # mm - length below bearing

# Magnet holder parameters (for AS5600)
MAGNET_DIAMETER = 6.0  # mm - standard neodymium magnet diameter
MAGNET_THICKNESS = 3.0  # mm - magnet thickness
MAGNET_HOLDER_DIAMETER = 10.0  # mm
MAGNET_HOLDER_HEIGHT = 4.0  # mm
MAGNET_CLEARANCE = 0.2  # mm - clearance for magnet pocket

# Wind vane parameters
VANE_LENGTH = 120.0  # mm - length of the tail fin
VANE_WIDTH = 80.0    # mm - width at widest point
VANE_THICKNESS = 2.0  # mm - thickness of the vane
VANE_OFFSET = 12.0   # mm - distance from shaft center to vane start (attaches to body edge)

# Arrow pointer parameters
ARROW_LENGTH = 60.0   # mm - length of pointer in front
ARROW_DIAMETER = 8.0  # mm - diameter of cylindrical arrow body
ARROW_TIP_LENGTH = 20.0  # mm - length of conical tip

# Counterweight parameters
COUNTERWEIGHT_DIAMETER = 15.0  # mm - diameter of counterweight
COUNTERWEIGHT_LENGTH = 25.0    # mm - length of counterweight
COUNTERWEIGHT_DISTANCE = 40.0  # mm - distance from shaft center

# Central body/hub parameters
BODY_LENGTH = 30.0     # mm - length of central body (fore-aft)
BODY_WIDTH = 18.0      # mm - width of central body (side-to-side)
BODY_HEIGHT = 15.0     # mm - height of central body (vertical)
BODY_TAPER = 0.6       # Taper ratio for aerodynamic teardrop shape

# Base mount parameters
BASE_DIAMETER = 50.0   # mm - diameter of base mount
BASE_HEIGHT = 10.0     # mm - height of base
BEARING_OD = 16.0      # mm - 688ZZ bearing outer diameter
BEARING_WIDTH = 5.0    # mm - 688ZZ bearing width
BEARING_CLEARANCE = 0.3  # mm - clearance for bearing fit

# AS5600 sensor positioning
SENSOR_DISTANCE = 1.0  # mm - distance between magnet and sensor (0.5-3mm optimal)

# === Helper Functions ===

def create_shaft():
    """Create the main rotating shaft"""
    shaft = Part.makeCylinder(SHAFT_DIAMETER/2, SHAFT_LENGTH)
    shaft.translate(Vector(0, 0, 0))
    return shaft

def create_magnet_holder():
    """Create the magnet holder at the bottom of the shaft"""
    # Main holder body
    holder = Part.makeCylinder(MAGNET_HOLDER_DIAMETER/2, MAGNET_HOLDER_HEIGHT)
    
    # Magnet pocket (slightly larger than magnet for fit)
    pocket_diameter = MAGNET_DIAMETER + MAGNET_CLEARANCE
    pocket_depth = MAGNET_THICKNESS + MAGNET_CLEARANCE
    pocket = Part.makeCylinder(pocket_diameter/2, pocket_depth)
    pocket.translate(Vector(0, 0, MAGNET_HOLDER_HEIGHT - pocket_depth))
    
    # Subtract pocket from holder
    holder = holder.cut(pocket)
    
    # Position at bottom of shaft
    holder.translate(Vector(0, 0, -MAGNET_HOLDER_HEIGHT))
    
    return holder

def create_wind_vane():
    """Create the wind vane (tail fin) - vertical airfoil-shaped"""
    # Create a vertical tapered vane shape (in XZ plane)
    # The vane should be vertical to catch the wind
    vane_base_z = SHAFT_LENGTH - 15  # Base height of vane
    vane_top_z = SHAFT_LENGTH + 5    # Top height of vane
    
    points = [
        Vector(VANE_OFFSET, 0, vane_base_z),
        Vector(VANE_OFFSET, 0, vane_top_z),
        Vector(VANE_OFFSET + VANE_LENGTH, 0, vane_top_z - 5),
        Vector(VANE_OFFSET + VANE_LENGTH, 0, vane_base_z + 5),
        Vector(VANE_OFFSET, 0, vane_base_z)
    ]
    
    # Create the face
    wire = Part.makePolygon(points)
    face = Part.Face(wire)
    
    # Extrude to thickness (in Y direction to make it a vertical fin)
    vane = face.extrude(Vector(0, VANE_THICKNESS, 0))
    # Center it
    vane.translate(Vector(0, -VANE_THICKNESS/2, 0))
    
    return vane

def create_arrow_pointer():
    """Create the arrow pointer - 3D cylindrical design"""
    arrow_z = SHAFT_LENGTH - 15  # Match vane base height
    
    # Create a cylindrical arrow body
    arrow_body_length = ARROW_LENGTH - ARROW_TIP_LENGTH
    arrow_body = Part.makeCylinder(ARROW_DIAMETER/2, arrow_body_length)
    arrow_body.rotate(Vector(0, 0, 0), Vector(0, 1, 0), 90)  # Align with X axis
    arrow_body.translate(Vector(-arrow_body_length, 0, arrow_z))
    
    # Create a conical tip
    arrow_tip = Part.makeCone(
        ARROW_DIAMETER/2,  # Base radius
        0.5,  # Tip radius (sharp point)
        ARROW_TIP_LENGTH
    )
    arrow_tip.rotate(Vector(0, 0, 0), Vector(0, 1, 0), 90)  # Point forward
    arrow_tip.translate(Vector(-ARROW_LENGTH, 0, arrow_z))
    
    # Combine body and tip
    arrow = arrow_body.fuse(arrow_tip)
    
    return arrow

def create_counterweight():
    """Create a counterweight to balance the vane"""
    arrow_z = SHAFT_LENGTH - 15  # Same height as arrow/vane
    
    # Create cylindrical counterweight
    counterweight = Part.makeCylinder(
        COUNTERWEIGHT_DIAMETER/2,
        COUNTERWEIGHT_LENGTH
    )
    counterweight.rotate(Vector(0, 0, 0), Vector(0, 1, 0), 90)
    counterweight.translate(Vector(-COUNTERWEIGHT_DISTANCE - COUNTERWEIGHT_LENGTH, 0, arrow_z))
    
    return counterweight

def create_central_body():
    """Create the central body/hub - streamlined aerodynamic shape"""
    body_z = SHAFT_LENGTH - 15  # Same base as arrow/vane
    
    # Create a streamlined body using a tapered cone design
    # No egg shape - just clean aerodynamic lines
    
    # Main body dimensions
    body_radius = max(ARROW_DIAMETER/2 + 2, min(BODY_WIDTH/2, BODY_HEIGHT/2))
    
    # Front cone (pointed nose) - from arrow back to full width
    front_length = BODY_LENGTH * 0.4
    front_nose = Part.makeCone(
        0.5,  # Sharp nose tip
        body_radius,  # Full body radius at back
        front_length
    )
    front_nose.rotate(Vector(0, 0, 0), Vector(0, 1, 0), 90)  # Point forward
    front_nose.translate(Vector(-front_length, 0, body_z + ARROW_DIAMETER/2))
    
    # Middle section - full diameter cylinder
    middle_length = BODY_LENGTH * 0.3
    middle = Part.makeCylinder(body_radius, middle_length)
    middle.rotate(Vector(0, 0, 0), Vector(0, 1, 0), 90)
    middle.translate(Vector(0, 0, body_z + ARROW_DIAMETER/2))
    
    # Rear cone - tapers back to connect with vane
    rear_length = VANE_OFFSET - middle_length + 2
    rear_end_radius = VANE_THICKNESS * 1.5
    rear = Part.makeCone(
        body_radius,
        rear_end_radius,
        rear_length
    )
    rear.rotate(Vector(0, 0, 0), Vector(0, 1, 0), 90)
    rear.translate(Vector(middle_length, 0, body_z + ARROW_DIAMETER/2))
    
    # Combine all sections
    try:
        body = front_nose.fuse([middle, rear])
    except:
        # Fallback to simple shape
        body = Part.makeCylinder(body_radius, BODY_LENGTH)
        body.rotate(Vector(0, 0, 0), Vector(0, 1, 0), 90)
        body.translate(Vector(-BODY_LENGTH/2, 0, body_z + ARROW_DIAMETER/2))
    
    # Create a hole for the shaft
    shaft_hole = Part.makeCylinder(SHAFT_DIAMETER/2 + 0.1, BODY_HEIGHT + 4)
    shaft_hole.translate(Vector(0, 0, body_z + ARROW_DIAMETER/2 - BODY_HEIGHT/2 - 2))
    
    # Subtract the shaft hole
    body = body.cut(shaft_hole)
    
    return body

def create_base_mount():
    """Create the base mount with bearing housing"""
    # Main base cylinder
    base = Part.makeCylinder(BASE_DIAMETER/2, BASE_HEIGHT)
    base.translate(Vector(0, 0, -BASE_HEIGHT))
    
    # Bearing housing hole
    bearing_hole_depth = BASE_HEIGHT/2 + 1
    bearing_hole = Part.makeCylinder(
        (BEARING_OD + BEARING_CLEARANCE)/2, 
        bearing_hole_depth
    )
    bearing_hole.translate(Vector(0, 0, -bearing_hole_depth))
    
    # Through hole for shaft
    shaft_hole = Part.makeCylinder(
        (SHAFT_DIAMETER + 0.5)/2,  # Slight clearance
        BASE_HEIGHT + 2
    )
    shaft_hole.translate(Vector(0, 0, -BASE_HEIGHT - 1))
    
    # Mounting holes (4 holes at 45° angles)
    mounting_holes = []
    hole_diameter = 3.5  # M3 screw
    hole_radius = BASE_DIAMETER/2 - 8
    
    for angle in [45, 135, 225, 315]:
        rad = math.radians(angle)
        x = hole_radius * math.cos(rad)
        y = hole_radius * math.sin(rad)
        hole = Part.makeCylinder(hole_diameter/2, BASE_HEIGHT + 2)
        hole.translate(Vector(x, y, -BASE_HEIGHT - 1))
        mounting_holes.append(hole)
    
    # Subtract all holes
    base = base.cut(bearing_hole)
    base = base.cut(shaft_hole)
    for hole in mounting_holes:
        base = base.cut(hole)
    
    return base

def create_sensor_mount_area():
    """Create a platform for mounting the AS5600 breakout board"""
    # Platform dimensions (typical AS5600 breakout is ~15x15mm)
    platform_width = 20.0
    platform_length = 20.0
    platform_height = 3.0
    
    platform = Part.makeBox(platform_length, platform_width, platform_height)
    platform.translate(Vector(
        -platform_length/2,
        -platform_width/2,
        -(MAGNET_HOLDER_HEIGHT + SENSOR_DISTANCE + platform_height)
    ))
    
    # Mounting holes for sensor board (2.54mm pitch)
    hole_spacing = 12.7  # Common spacing for breakout boards
    hole_diameter = 2.0  # For M2 screws or pins
    
    holes_positions = [
        (-hole_spacing/2, -hole_spacing/2),
        (hole_spacing/2, -hole_spacing/2),
        (-hole_spacing/2, hole_spacing/2),
        (hole_spacing/2, hole_spacing/2)
    ]
    
    for x, y in holes_positions:
        hole = Part.makeCylinder(hole_diameter/2, platform_height + 1)
        hole.translate(Vector(x, y, -(MAGNET_HOLDER_HEIGHT + SENSOR_DISTANCE + platform_height) - 0.5))
        platform = platform.cut(hole)
    
    return platform

# === Main Assembly Function ===

def create_wind_vane_assembly():
    """Create the complete wind vane assembly"""
    doc = App.ActiveDocument
    if doc is None:
        doc = App.newDocument("WindVane")
    
    print("Creating Wind Vane for AS5600...")
    
    # Create all components
    print("- Creating shaft...")
    shaft = create_shaft()
    
    print("- Creating magnet holder...")
    magnet_holder = create_magnet_holder()
    
    print("- Creating wind vane...")
    vane = create_wind_vane()
    
    print("- Creating arrow pointer...")
    arrow = create_arrow_pointer()
    
    print("- Creating counterweight...")
    counterweight = create_counterweight()
    
    print("- Creating central body...")
    body = create_central_body()
    
    print("- Creating base mount...")
    base = create_base_mount()
    
    print("- Creating sensor mount...")
    sensor_mount = create_sensor_mount_area()
    
    # Combine rotating parts
    rotating_assembly = shaft.fuse([magnet_holder, vane, arrow, counterweight, body])
    
    # Add components to document
    obj_rotating = doc.addObject("Part::Feature", "RotatingAssembly")
    obj_rotating.Shape = rotating_assembly
    obj_rotating.ViewObject.ShapeColor = (0.8, 0.8, 0.9)  # Light blue
    
    obj_base = doc.addObject("Part::Feature", "BaseMounting")
    obj_base.Shape = base
    obj_base.ViewObject.ShapeColor = (0.7, 0.7, 0.7)  # Gray
    
    obj_sensor = doc.addObject("Part::Feature", "SensorMount")
    obj_sensor.Shape = sensor_mount
    obj_sensor.ViewObject.ShapeColor = (0.9, 0.7, 0.5)  # Tan
    
    # Add reference magnet (for visualization)
    magnet_viz = Part.makeCylinder(MAGNET_DIAMETER/2, MAGNET_THICKNESS)
    magnet_viz.translate(Vector(0, 0, -MAGNET_HOLDER_HEIGHT + MAGNET_CLEARANCE/2))
    obj_magnet = doc.addObject("Part::Feature", "Magnet_Reference")
    obj_magnet.Shape = magnet_viz
    obj_magnet.ViewObject.ShapeColor = (0.1, 0.1, 0.1)  # Dark (magnet)
    obj_magnet.ViewObject.Transparency = 50
    
    doc.recompute()
    App.ActiveDocument.recompute()
    
    # Fit view
    try:
        import FreeCADGui
        FreeCADGui.SendMsgToActiveView("ViewFit")
    except:
        pass
    
    print("\n=== Wind Vane Assembly Complete ===")
    print(f"Shaft diameter: {SHAFT_DIAMETER}mm")
    print(f"Magnet: {MAGNET_DIAMETER}mm x {MAGNET_THICKNESS}mm (round neodymium)")
    print(f"Bearing: 688ZZ ({SHAFT_DIAMETER}mm ID, {BEARING_OD}mm OD, {BEARING_WIDTH}mm width)")
    print(f"Sensor clearance: {SENSOR_DISTANCE}mm")
    print(f"\nThe arrow points INTO the wind direction.")
    print(f"The vane (tail) will be blown downwind.")
    print("\nNext steps:")
    print("1. Export STL files for 3D printing")
    print("2. Install 688ZZ bearing in base")
    print("3. Insert round neodymium magnet into holder")
    print("4. Mount AS5600 breakout board on sensor mount")
    print("5. Ensure magnet-to-sensor distance is 0.5-3mm")
    print("6. Adjust counterweight if needed for balance")
    
    return doc

# === Execute ===
if __name__ == "__main__":
    create_wind_vane_assembly()
