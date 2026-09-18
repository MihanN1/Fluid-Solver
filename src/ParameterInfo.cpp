#include "ParameterInfo.hpp"

namespace maskui {

const char* parameterKey(std::size_t index) {
    static constexpr std::array<const char*, ParameterCount> keys{{
        "U0", "nu", "ro",
        "phases", "rho1", "nu1", "rho2", "nu2",
        "phaseInit", "phaseLevel", "phaseX", "phaseY", "phaseZ", "vofScheme",
        "mixing", "diffusivity", "surfaceTension", "contactAngle", "sources", "vortices",
        "gravityEnabled", "gravityAccel", "gravityAngle", "gravityTilt",
        "gravityMode",
        "regime", "gamma", "R", "gamma2", "R2", "T0", "pInf", "machInlet",
        "speciesMode", "gridStretch", "stretchRatio", "refineNear",
        "amrLevels", "amrCriterion", "amrThreshold", "amrEvery",
        "acousticFields", "acousticWindow", "acousticRef", "microphones",
        "micInterval", "micAudio", "micAudioRate", "micAudioSpeed",
        "turbulence", "Cs", "turbIntensity", "turbLengthScale",
        "sliceAngleX", "sliceAngleY", "sliceAngleZ", "sliceRotation",
        "profiles",
        "caseType", "lidSpeed",
        "bcLeft", "bcRight", "bcBottom", "bcTop", "bcFront", "bcBack",
        "bcLeftSpeed", "bcRightSpeed", "bcBottomSpeed", "bcTopSpeed",
        "bcFrontSpeed", "bcBackSpeed",
        "inletFrom", "inletTo", "inletFrom2", "inletTo2", "inletProfile",
        "wallMotion",
        "uiBody",
        "uiBodyX", "uiBodyY", "uiBodyZ", "uiBodySize",
        "uiBodyTiltX", "uiBodyTiltY", "uiBodyTiltZ", "uiBodyTurnPlane",
        "uiBodyMoveAxis", "uiBodyMoveBy", "uiBodyTurnAxis", "uiBodyTurnBy",
        "uiBodyBehaviour", "uiBodyRot", "uiBodyRotX", "uiBodyRotY",
        "uiBodySlideX", "uiBodySlideY", "uiBodySlideZ",
        "uiBodyVx", "uiBodyVy", "uiBodyVz",
        "uiBodySpin", "uiBodySpinX", "uiBodySpinY",
        "uiBodyMass", "uiBodyDensity", "uiBodyInertiaX", "uiBodyInertiaY",
        "uiBodyPins", "uiBodyPinZ", "uiBodyPinRotX", "uiBodyPinRotY",
        "bodyMotion", "uiBodyTrack", "uiBodyPath", "bodyCoupling",
        "bodyCollisions", "bodyRestitution", "bodyForceReport",
        "Lx", "Ly", "Lz", "nx", "ny", "nz",
        "CFL", "totalTime", "steadyTolerance", "addTime",
        "dtUpdateInterval", "dtSafety",
        "convection", "limiter", "timeScheme",
        "omega", "smootherOmega", "mgIterations", "mgTolerance",
        "mgMinCoarseSize", "runName", "saveInterval", "extraFields",
        "useCuda", "useAvx2", "useOpenMP",
        "threads", "uiCacheMB"
    }};
    return index < keys.size() ? keys[index] : "unknown";
}

std::size_t parameterIndexForKey(const std::string& key) {
    for (std::size_t index = 0; index < ParameterCount; ++index)
        if (key == parameterKey(index))
            return index;
    return ParameterCount;
}

const char* parameterHint(std::size_t index) {
    switch (index) {
    case WindSpeed:
        return "How fast the fluid comes in. Sets the Reynolds number with the size and the viscosity. m/s.";
    case Viscosity:
        return "How syrupy the fluid is. Low means it swirls and sheds vortices, high means it flows smoothly. Water 1e-6, air 1.5e-5 m2/s.";
    case Density:
        return "How heavy the fluid is per cubic metre. Water 1000, air 1.225 kg/m3. Scales the pressure the run reports.";
    case Phases:
        return "1 is one fluid. 2 tracks two of them and carries where each one is, so you can pour, splash and mix.";
    case Density1:
        return "Density of the fluid the start shape is made of, and the one the brush paints. Water 1000 kg/m3.";
    case Viscosity1:
        return "Viscosity of that first fluid. Water 1e-6 m2/s.";
    case Density2:
        return "Density of whatever fills the rest of the box. Air 1.225 kg/m3.";
    case Viscosity2:
        return "Viscosity of that second fluid. Air 1.5e-5 m2/s.";
    case PhaseInitKind:
        return "Where fluid 1 starts: a layer at the bottom, a round drop, or a column against one wall. Painting overrides it.";
    case PhaseLevel:
        return "How much of the box the start shape fills, as a fraction. 0.5 is half.";
    case PhaseSpotX:
        return "Where the drop sits across the box, as a fraction of the width.";
    case PhaseSpotY:
        return "Where the drop sits up the box, as a fraction of the height.";
    case PhaseSpotZ:
        return "Where the drop sits through the box, as a fraction of the depth. One plane has only z = 0.5.";
    case VofSchemeKind:
        return "How sharply the boundary between the two fluids is kept. hric and cicsam stay sharp, upwind smears it out.";
    case MixingKindRow:
        return "immiscible is oil and water with a real surface between them. miscible is ink in water, which just spreads.";
    case Diffusivity:
        return "How fast one fluid spreads through the other once they mix. Salt in water 1e-9, gas in gas 1e-5 m2/s.";
    case SurfaceTension:
        return "The skin that pulls a droplet round. Water against air 72, mercury 485 mN/m. Zero switches it off.";
    case ContactAngle:
        return "How a wall treats fluid 1: under 90 degrees it wets and creeps up, over 90 it beads off like water on wax.";
    case SourceLine:
        return "Discs inside the box that pump fluid out of themselves - a jet, a vent, a leak. Needs an outlet somewhere.";
    case GravityEnabled:
        return "Turns gravity on. With one fluid it only tilts the pressure; with two it is what makes the heavy one sink.";
    case GravityAccel:
        return "How strong gravity is. 9.81 is Earth, 1.62 the Moon, 0 free fall. m/s2.";
    case GravityAngle:
        return "Which way is down, in degrees clockwise from straight down. 90 pulls sideways.";
    case GravityTilt:
        return "Tips that direction out of the xy plane towards +z, in degrees. 0 keeps gravity in the plane.";
    case GravityMode:
        return "full uses the real density everywhere. reduced subtracts the average first, which is steadier at small density differences.";
    case RegimeKind:
        return "incompressible for water and slow air - fast, no sound. compressible for shocks, high speed and anything you want to hear.";
    case Gamma1:
        return "Ratio of specific heats of the gas. Air 1.4, helium 1.667, steam about 1.3. Fixes the speed of sound with R.";
    case GasConstant1:
        return "Gas constant of the gas. Air 287, helium 2077 J/(kg K). Light gases have big numbers and fast sound.";
    case Gamma2:
        return "Same for the second gas, read only when there are two of them. Helium 1.667.";
    case GasConstant2:
        return "Gas constant of the second gas. Helium 2077 J/(kg K), which is why it sounds high.";
    case Temperature0:
        return "Reference temperature in kelvin. 288 is a mild day. Sets the density and the speed of sound of the whole run.";
    case AmbientPressure:
        return "Background pressure in pascals. 101325 is one atmosphere at sea level.";
    case MachInlet:
        return "Inlet speed as a multiple of the speed of sound, not in m/s. Under 1 is subsonic, over 1 is supersonic.";
    case SpeciesModeRow:
        return "active lets the mixture's own properties change as the gases blend. passive freezes them at the first gas.";
    case GridStretchKind:
        return "Lets cells change size across the box, so detail goes where it is needed and the far field is cheap.";
    case StretchRatio:
        return "How much bigger one cell may be than its neighbour. 1.05 is five percent and the usual ceiling.";
    case RefineNear:
        return "How wide the fine band is, as a fraction of the box. 0.25 keeps a quarter of it at full resolution.";
    case AmrLevels:
        return "Puts patches of a finer grid where the flow needs them and moves them as it moves. 0 is off.";
    case AmrCriterionKind:
        return "What counts as worth refining: shocks, wakes, where two gases meet, the skin of a body, or all of it.";
    case AmrThreshold:
        return "How steep a feature has to be before it gets a patch, as a fraction of the steepest one in the box.";
    case AmrEvery:
        return "How many steps between rebuilding the patches. Small numbers keep them under the feature they follow.";
    case AcousticFields:
        return "Paints loudness and pitch onto the grid so you can see where the noise comes from.";
    case AcousticWindow:
        return "How far back the running average looks when it works out the loudness. Seconds.";
    case AcousticRef:
        return "The pressure that counts as 0 dB. 2e-5 Pa is the quietest sound a person can hear.";
    case MicrophoneLine:
        return "Points that record the pressure over time, like putting a microphone in the flow. x=0.5,y=0.2 in metres.";
    case MicInterval:
        return "How many steps between microphone samples. 1 records everything and catches the highest pitches.";
    case MicAudio:
        return "Also writes each microphone as a .wav you can actually play, next to the frames.";
    case MicAudioRate:
        return "Sample rate of those .wav files. 44100 is what every player expects.";
    case MicAudioSpeed:
        return "Playback speed. 1 is real time. 0.05 stretches it twenty times, which is how you hear two milliseconds of flow.";
    case TurbulenceKindRow:
        return "Accounts for eddies too small for the grid. none solves only what fits; use a model above Reynolds a few thousand.";
    case SmagorinskyCs:
        return "How strongly the large-eddy model damps. 0.17 is the textbook value, lower is gentler.";
    case TurbIntensity:
        return "How gusty the incoming flow already is, as a fraction of its speed. 0.05 is a typical wind tunnel.";
    case TurbLengthScale:
        return "The size of the biggest incoming eddies, in metres. Roughly the size of whatever stirred the flow upstream.";
    case SliceX:
        return "Tilts the cutting plane through your 3D model about the X axis, to change which cross-section becomes the shape.";
    case SliceY:
        return "Tilts that cutting plane about the Y axis, which is the one that swings the model's nose round.";
    case SliceZ:
        return "Tilts that cutting plane about the Z axis.";
    case SliceRotation:
        return "Spins the cut shape inside the plane, so the profile sits at the angle of attack you want.";
    case Profiles:
        return "Places several models in the box at once, each with its own position and size.";
    case CaseKind:
        return "A ready-made setup: channel is flow through a duct, cavity is a box with a sliding lid, shockTube is a burst diaphragm.";
    case LidSpeed:
        return "How fast the lid of the cavity slides, dragging the fluid round with it. m/s.";
    case BcLeft:
        return "What the left edge of the box is: an inlet, an outlet, a solid wall, or a slippery one.";
    case BcRight:
        return "What the right edge is. A run needs somewhere for the fluid to leave, usually an outlet here.";
    case BcBottom:
        return "What the bottom edge is.";
    case BcTop:
        return "What the top edge is.";
    case BcFront:
        return "What the front face at z = 0 is. It only does anything once the run is more than one plane deep.";
    case BcBack:
        return "What the back face at z = Lz is. Slip on both is what every one-plane run has always had.";
    case BcLeftSpeed:
        return "How fast the left edge blows or slides, in m/s. Only read for an inlet or a moving wall.";
    case BcRightSpeed:
        return "How fast the right edge blows or slides, in m/s.";
    case BcBottomSpeed:
        return "How fast the bottom edge blows or slides, in m/s.";
    case BcTopSpeed:
        return "How fast the top edge blows or slides, in m/s.";
    case BcFrontSpeed:
        return "How fast the front face blows or slides, in m/s.";
    case BcBackSpeed:
        return "How fast the back face blows or slides, in m/s.";
    case InletFrom:
        return "Where the inlet opening starts up the edge, as a fraction. Use it to blow through a slot rather than the whole side.";
    case InletTo:
        return "Where that opening ends, as a fraction of the edge.";
    case InletFrom2:
        return "Where the opening starts across the depth, as a fraction. With the pair above it cuts a window, not a band.";
    case InletTo2:
        return "Where that window ends across the depth, as a fraction of the side.";
    case InletProfileKind:
        return "Shape of the incoming speed across the opening: flat, a parabola across the band, or one across the depth too.";
    case WallMotionLine:
        return "Makes a body's surface move without the body going anywhere - a conveyor belt or a spinning roller.";
    case BodySelect:
        return "Which body the rows below are about. Bodies are numbered by the mask, left to right and bottom to top.";
    case BodyBehaviour:
        return "What this body does: sit still, drag the fluid past its surface, let it slip, travel on a set path, or move freely.";
    case BodyPlaceX:
        return "Where the centre of this model sits along x, in metres. Empty means the middle of the domain.";
    case BodyPlaceY:
        return "Where the centre of this model sits along y, in metres. Empty means the middle of the domain.";
    case BodyPlaceZ:
        return "Where the centre of this model sits along z, in metres. Read once nz is above 1.";
    case BodyPlaceSize:
        return "The longest side of this model after it is scaled, in metres. 0 means a fifth of the smallest side of the domain.";
    case BodyTiltX:
        return "Turn this model about the x axis before it is voxelised, in degrees.";
    case BodyTiltY:
        return "Turn this model about the y axis before it is voxelised, in degrees.";
    case BodyTiltZ:
        return "Turn this model about the z axis before it is voxelised, in degrees.";
    case BodyTurnPlane:
        return "Turn this model inside the plane it is cut on, in degrees. This is the one that means something at nz = 1.";
    case BodyMoveAxis:
        return "Which axis Move by runs along.";
    case BodyMoveBy:
        return "Type how far to move the selected model along that axis, in metres. It is added to the position and this box goes back to zero.";
    case BodyTurnAxis:
        return "Which axis Turn by turns about. plane is the rotation inside the cut plane.";
    case BodyTurnBy:
        return "Type how far to turn the selected model about that axis, in degrees. It is added to the angle and this box goes back to zero.";
    case BodyRotation:
        return "How fast the surface spins in place, in degrees per second. The body itself stays put.";
    case BodyRotationX:
        return "How fast the surface rolls about the x axis through its own centre, in degrees per second.";
    case BodyRotationY:
        return "How fast the surface rolls about the y axis through its own centre, in degrees per second.";
    case BodySlideX:
        return "How fast the surface slides sideways while the body stays put, like a treadmill. m/s.";
    case BodySlideY:
        return "How fast the surface slides up or down while the body stays put. m/s.";
    case BodySlideZ:
        return "How fast the surface runs along +z while the body stays put. m/s.";
    case BodyVelocityX:
        return "How fast the body itself travels sideways. Under free, the speed it starts with. m/s.";
    case BodyVelocityY:
        return "How fast the body itself travels up or down. m/s.";
    case BodyVelocityZ:
        return "How fast the body itself travels along z, in and out of the plane. m/s.";
    case BodySpin:
        return "How fast the body itself turns, in degrees per second.";
    case BodySpinX:
        return "How fast the body itself rolls about the x axis, in degrees per second.";
    case BodySpinY:
        return "How fast the body itself rolls about the y axis, in degrees per second.";
    case BodyMass:
        return "Mass of a free body per metre of depth. Heavier bodies are pushed around less. kg/m.";
    case BodyDensity:
        return "Density of a free body, used instead of the mass if you set it. Aluminium 2700, wood 600 kg/m3.";
    case BodyInertiaX:
        return "How hard a free body is to roll about x. Zero lets the solver take it from the shape.";
    case BodyInertiaY:
        return "How hard a free body is to roll about y. Zero lets the solver take it from the shape.";
    case BodyPins:
        return "Locks a free body's degrees of freedom, so it can only slide one way, or only spin, or is held completely.";
    case BodyPinZ:
        return "Holds a free body in its plane: it may drift about inside the plane but never out of it.";
    case BodyPinRotX:
        return "Holds a free body's roll about x, so the flow cannot tip it out of the plane that way.";
    case BodyPinRotY:
        return "Holds a free body's roll about y, so the flow cannot tip it out of the plane that way.";
    case BodyMotionLine:
        return "The raw motion line for every body at once. The rows above write into it; editing it by hand also works.";
    case BodyTrackLine:
        return "The keyframed poses the Layout view records. It never reaches the solver - it is what the motion line is rebuilt from.";
    case BodyPathKind:
        return "How those poses become a path: a smooth curve through them all, or a straight leg from each one to the next.";
    case BodyCouplingKind:
        return "How hard the fluid and a free body are made to agree each step. added is the sane default, strong is for light bodies.";
    case BodyCollisions:
        return "Lets bodies bounce off each other and off the walls instead of passing straight through.";
    case BodyRestitution:
        return "How bouncy a collision is. 1 keeps all the speed, 0 is a dead stop, 0.2 is a realistic thud.";
    case BodyForceReport:
        return "Prints the lift and drag on bodies whose path you set, so you can read the force off a prescribed motion.";
    case DomainX:
        return "How wide the simulated box is, in metres. Everything else is measured against it.";
    case DomainY:
        return "How tall the simulated box is, in metres.";
    case DomainZ:
        return "How deep the simulated box is, in metres. At one plane it is the depth the forces are quoted per.";
    case CellsX:
        return "How many cells across. More cells means finer detail and more time. Cost grows faster than the number.";
    case CellsY:
        return "How many cells up. Keep the cell roughly square: nx/Lx close to ny/Ly.";
    case CellsZ:
        return "How many cells deep. 1 is one plane, the way every earlier version ran; above 1 the run is a volume.";
    case Cfl:
        return "How far the flow may cross a cell in one step. Under 1 keeps it stable; lower is safer and slower.";
    case TotalTime:
        return "How many seconds of flow to simulate. Not how long it takes to run.";
    case SteadyTolerance:
        return "Stops early once the flow stops changing by more than this. 0 runs the full time no matter what.";
    case AddTime:
        return "When continuing a finished run, how many more seconds to carry it on for.";
    case DtUpdateInterval:
        return "How many steps between recalculating the time step. 1 is safest, higher saves a little work.";
    case DtSafety:
        return "Extra margin on the time step. 0.8 means take 80% of the largest step that looks stable.";
    case Convection:
        return "How the flow carries itself along. muscl is accurate, upwind is blunt but unbreakable, central is sharp and fragile.";
    case Limiter:
        return "Keeps the accurate scheme from overshooting at a sharp edge. minmod is cautious, superbee is the sharpest.";
    case TimeSchemeKind:
        return "How the run steps forward in time. rk2 and rk3 are more accurate per step, euler is the cheapest.";
    case CoarseSorOmega:
        return "Relaxation on the coarsest pressure grid. 1.7 is usual; too high oscillates, too low crawls.";
    case SmootherOmega:
        return "Relaxation on the finer pressure grids. Around 1 is right for the red-black smoother.";
    case MgIterations:
        return "How many pressure passes are allowed per step before it gives up and moves on.";
    case MgTolerance:
        return "How exactly the pressure has to balance before the step is accepted. Smaller is more accurate and slower.";
    case MgMinCoarseSize:
        return "How small the coarsest pressure grid is allowed to get. 8 cells a side is the usual floor.";
    case RunName:
        return "What to call this run. It becomes the folder the frames go in, so two runs do not land on top of each other and you can tell them apart a week later.";
    case SaveInterval:
        return "How many steps between saved frames. Fewer frames means a smaller output folder and a choppier animation.";
    case VortexLine:
        return "Vortices put into the flow before the first step, so there is something for the Vortices view to draw without waiting for a body to shed one.";
    case ExtraFields:
        return "Extra quantities written into every frame - vorticity, Mach, temperature, loudness - so you can colour by them later.";
    case UseCuda:
        return "Runs the pressure solve on the graphics card. Needs a CUDA build and an NVIDIA card.";
    case UseAvx2:
        return "Uses the wide instructions modern CPUs have. Off falls back to plain loops that do the same arithmetic.";
    case UseOpenMp:
        return "Spreads the work over CPU cores. Off runs on one core, which is slower but easier to compare.";
    case SolverThreads:
        return "How many cores to use. 0 lets it take everything the machine has.";
    case CacheMegabytes:
        return "How much memory this window may spend keeping loaded frames around for instant scrubbing.";
    default:
        return "";
    }
}

std::string parameterHelp(std::size_t index) {
    switch (index) {
    case WindSpeed:
        return "U0: inlet speed used by the Fluid Solver.";
    case Viscosity:
        return "nu: kinematic viscosity; affects Reynolds number and diffusion timestep.";
    case Density:
        return "rho / CLI key ro: physical density used by the solver pressure output.";
    case Phases:
        return "phases: 1 is one fluid and every run before this one. 2 turns on the volume fraction, and then rho and nu are ignored in favour of the two fluids below.";
    case Density1:
    case Viscosity1:
        return "Fluid 1 is what the start shape is made of, and what the brush paints. Water is 1000 kg/m3 and 1e-6 m2/s.";
    case Density2:
    case Viscosity2:
        return "Fluid 2 fills everything the start shape does not. Air is 1.225 kg/m3 and 1.5e-5 m2/s.";
    case PhaseInitKind:
        return "What is in the domain at the start: layer fills the bottom, drop is a circle, column is the dam break block. Painting overrides all three.";
    case PhaseLevel:
        return "Layer height, drop diameter or column height, as a fraction of the domain.";
    case PhaseSpotX:
    case PhaseSpotY:
    case PhaseSpotZ:
        return "Where the drop sits, or how wide the column is, as fractions of Lx, Ly and Lz. phaseZ is only read once nz is above 1; at one plane the drop is a disc through the whole of it.";
    case VofSchemeKind:
        return "How the interface is carried. hric and cicsam both push it back together every step; upwind smears it over ten cells and is here to be compared against.";
    case MixingKindRow:
        return "mixing: immiscible is oil and water, a surface between them that surface tension can pull on. miscible is ink and water, no surface at all - the composition spreads by diffusion instead and the interface scheme above is not read.";
    case Diffusivity:
        return "diffusivity: how fast one fluid spreads through the other, m2/s, and only read when they mix. Salt in water is about 1e-9, a gas into another gas about 1e-5.";
    case SurfaceTension:
        return "surfaceTension: sigma, in mN/m because that is how everybody quotes it. Water against air is 72, mercury is 485, a soap film is about 25. Zero is off and the whole curvature pass is skipped. It costs time steps as well as time: dt has to stay under sqrt((rho1+rho2)*dx^3/(4*pi*sigma)) or the smallest capillary wave the grid can hold blows up, and the solver says so on the first step.";
    case ContactAngle:
        return "contactAngle: degrees, measured inside fluid 1, at a solid wall. 90 is a wall neither fluid prefers, under 90 means fluid 1 wets it and creeps up, over 90 means it beads off.";
    case SourceLine:
        return "sources, in the solver's own grammar: x=0.5,y=0.2,r=0.05,rate=2,angle=90,phase=1 - a disc inside the domain that pushes fluid out of itself, a ball of one once nz>1. Needs an outlet like an inlet does. z=<m> puts the centre off the first plane and is 0 when left out; elev=<deg> lifts the jet out of the xy plane towards +z, so elev=90 fires straight along z whatever angle says.";
    case GravityEnabled:
        return "gravityEnabled: adds gravity as a uniform body force. At constant density it moves the pressure map, not the velocity field.";
    case GravityAccel:
        return "gravityAccel: magnitude of that body force in m/s2. 9.81 is Earth.";
    case GravityAngle:
        return "gravityAngle: direction in degrees, measured clockwise from straight down.";
    case GravityTilt:
        return "gravityTilt: how far that direction is tipped out of the xy plane towards +z, in degrees. Zero is every run before this one - gravity stays in the plane and the third component is exactly nought. 90 pulls straight along +z and the angle above stops meaning anything.";
    case RegimeKind:
        return "incompressible is the projection solver and every run before this one: density is a constant, the pressure comes out of a Poisson solve, and the speed of sound is infinite. compressible is a second solver entirely - density is one of the unknowns, there is no pressure solve at all, and sound travels at a finite speed, which is what lets a shock exist and what lets the run have something to listen to. Picking it puts the multigrid rows out of use and brings the gas rows in.";
    case Gamma1:
    case GasConstant1:
        return "The gas. gamma is the ratio of specific heats - 1.4 for air, 1.667 for helium or argon, about 1.3 for steam - and R is the specific gas constant, 287 for air and 2077 for helium. Together they fix the speed of sound: sqrt(gamma*R*T).";
    case Gamma2:
    case GasConstant2:
        return "The second gas, read only at two phases. Helium in air is the demonstration everybody knows: same pressure, same temperature, sound travels nearly three times faster through it.";
    case Temperature0:
        return "T0: the reference temperature in kelvin. The inlet and the initial field are both built from it and the ambient pressure through the gas law, so it sets the density and the speed of sound of the whole run.";
    case AmbientPressure:
        return "pInf: the ambient pressure in pascals. 101325 is one atmosphere. It is what an outlet holds the flow to while it is subsonic, and what the initial field is built at.";
    case MachInlet:
        return "How fast the inlet blows, as a multiple of the speed of sound there rather than in m/s - because in a compressible run that ratio is the number that decides the physics. Under 1 the boundary still hears the domain and holds the pressure; over 1 nothing travels back out and the whole state is imposed.";
    case SpeciesModeRow:
        return "active lets the composition set gamma and R, so the speed of sound, the temperature and every wave speed follow the mixture. passive carries the fraction along and nothing else, with the properties frozen at the first gas - useful only for showing what the thermodynamics is worth, and the solver says so in its own log when it is on.";
    case AcousticFields:
        return "Writes the sound out as fields on the grid: the pressure fluctuation, its level in dB and a pitch in Hz for every cell. It costs four arrays and a running average per step, and it is off by default because most runs are not about the noise.";
    case AcousticWindow:
        return "How far back the running average looks, in seconds. It has to cover several periods of whatever you are listening for, and it is what separates the sound from the flow: anything slower than this window counts as the flow and is subtracted off.";
    case AcousticRef:
        return "The pressure that counts as 0 dB. 2e-5 Pa is the human hearing threshold and what SPL is always quoted against; 1 Pa is the other common choice and shifts every number by 94 dB.";
    case MicrophoneLine:
        return "Points that record the pressure every few steps: x=0.5,y=0.2;x=1,y=0.5 in metres, and x=0.5,y=0.2,z=0.3 once the run is a volume. z is the one you may leave out: without it the point sits at 0, which is the single plane a nz=1 run has and where every microphone written before this one was. At the end the run writes microphones.txt next to the frames with the whole trace and, for each point, a level and a peak frequency found by scanning the spectrum. This is the accurate half of the acoustics; the fields are the half you can look at.";
    case MicAudio:
        return "Writes what each microphone heard as microphone1.wav, microphone2.wav and so on, next to the frames, so you can play it instead of reading a column of numbers. The trace is de-meaned, box-filtered down to the file's rate rather than plain decimated - dropping samples out of a megahertz signal folds everything above the new Nyquist back into the audible band as a screech that was never there - and peak-normalised, with the pascal value that ended up at full scale printed when the run stops.";
    case MicAudioRate:
        return "Sample rate of those .wav files. 44100 is what everything plays. There is nothing to gain above twice the highest frequency in the run, and the run's own ceiling is 1/(micInterval*dt).";
    case MicAudioSpeed:
        return "Stretches the timebase of the .wav. 1 is real time. 0.05 plays it twenty times slower, which drops every frequency by twenty as well - that is how you hear something that happened in two milliseconds, or how you bring a 40 kHz whistle down to where your ears are. It does not change the simulation, only the file.";
    case MicInterval:
        return "How many steps pass between microphone samples. 1 is every step and sets the sampling rate to 1/dt, which is the highest frequency the run can resolve at all. Raising it makes the file smaller and the ceiling lower.";
    case TurbulenceKindRow:
        return "none solves what is actually on the grid and nothing else, which is right until the grid stops being able to hold the smallest eddy that matters. smagorinsky is a large eddy model: it adds the viscosity the eddies smaller than a cell would have had, and it wants a grid fine enough that they really are small. kOmegaSST carries two more transported fields and models all of the turbulence rather than the small end of it, which is what a Reynolds number in the millions on a grid you can afford needs.";
    case SmagorinskyCs:
        return "Cs: the one constant Smagorinsky has. 0.17 is what it comes out at for isotropic turbulence, 0.1 is what channels and anything with a wall in it want, and near a wall it gets damped down from there anyway.";
    case TurbIntensity:
        return "How turbulent the inlet is, as a fraction of its speed. 0.01 is a wind tunnel, 0.05 is a pipe, 0.1 is behind something. Only kOmegaSST reads it: it sets k = 1.5*(I*U)^2 coming in.";
    case TurbLengthScale:
        return "The size of the biggest eddy coming in through the inlet, in metres. A tenth of the pipe or the duct is the usual guess, and zero lets the solver take a tenth of Ly. Only kOmegaSST reads it: with the intensity above it sets omega coming in.";
    case SliceX:
    case SliceY:
    case SliceZ:
    case SliceRotation:
        return "Geometry section transform. The UI bakes this into section-adapter.obj.";
    case GravityMode:
        return "gravityMode: reduced adds the head on output only, which is exact at one density. body puts the force in the solve and p becomes the total pressure.";
    case Profiles:
        return "profiles: several models at once, each placed where you say. <file>@x=1,y=0.5,size=0.3;<file>@x=3,y=0.5 - the separator is @ because a Windows path owns the colon.";
    case CaseKind:
        return "caseType: channel leaves the four sides to you. cavity is the lid driven cavity - four walls, the top one sliding - and it sets all four sides itself, so the boundary rows below are not sent when it is picked.";
    case LidSpeed:
        return "lidSpeed: how fast the cavity lid slides. Re = lidSpeed * Ly / nu, and 1 m/s with nu = 0.01 over a 1 m box is the Re 100 case everybody checks against Ghia.";
    case SteadyTolerance:
        return "steadyTolerance: stop early once the field stops changing. It is the largest velocity change per second divided by whatever drives the case, so 1e-5 means one part in a hundred thousand. Zero runs the whole of Total time.";
    case BcLeft:
    case BcRight:
    case BcBottom:
    case BcTop:
        return "What this side of the domain does. A run needs at least one outlet, or the pressure has no level to sit at.";
    case BcFront:
    case BcBack:
        return "What the two faces the third dimension added do. They exist at one plane as well, where slip on both is exactly the 2D case: the flow never leaves the plane and nothing is dragged. Anything else there would put a boundary layer on a face a single cell deep, which is why they are not offered until nz is above 1.";
    case BcLeftSpeed:
    case BcRightSpeed:
    case BcBottomSpeed:
    case BcTopSpeed:
    case BcFrontSpeed:
    case BcBackSpeed:
        return "Speed this side imposes: what a movingWall slides at, or an inlet speed other than U0. Left at zero on an inlet it means U0.";
    case InletFrom:
    case InletTo:
        return "Cuts the inlet down to a band of its side, as fractions measured from the low end. The rest of that side becomes a wall.";
    case InletFrom2:
    case InletTo2:
        return "The same cut along the other way across the face, so the two pairs together make a window rather than a band. Left and right span y then z, bottom and top span x then z, front and back span x then y. At one plane there is only one cell that way and these are not read.";
    case InletProfileKind:
        return "uniform is a flat inlet, parabolic bends it into a parabola across the band carrying the same flow rate, and parabolicSpan bends it across the depth as well - the two parabolas multiplied, which is what a duct with walls on all four sides actually has. At one plane there is nothing to resolve across the depth and parabolicSpan is the plain parabola again.";
    case BodySelect:
        return "Which body the rows under it are about. The numbers are the ones the solver prints for the mask, counted the same way - flood filled in scan order. The two text rows at the bottom are what is actually sent; these rows write into them.";
    case BodyBehaviour:
        return "static leaves the body alone. drag holds the fluid and pulls it along without the body going anywhere - that is wallMotion. slip lets the fluid past and exerts no drag. travel moves the body itself along a path you give. free lets go of it and the flow decides where it goes.";
    case BodyPlaceX:
    case BodyPlaceY:
    case BodyPlaceZ:
    case BodyPlaceSize:
    case BodyTiltX:
    case BodyTiltY:
    case BodyTiltZ:
    case BodyTurnPlane:
        return "Where the selected model sits in the domain and how it is turned before it is voxelised, which is the profiles line written out as rows. Left at their defaults a model lands in the middle of the domain at a fifth of its smallest side. A body touching the domain edge is a wall rather than an obstacle and is refused with the number it missed by, so the useful range stops short of the walls.";
    case BodyMoveAxis:
    case BodyMoveBy:
    case BodyTurnAxis:
    case BodyTurnBy:
        return "Move and turn the selected model by typing how far, the way a 3D package does it: pick the axis, type the amount, and it is added to where the model already is. The box goes back to zero afterwards, so typing the same number again moves it the same distance again. The rows above show where it ended up, and can be typed into directly when the absolute position is what you know.";
    case BodyRotation:
    case BodyRotationX:
    case BodyRotationY:
    case BodySlideX:
    case BodySlideY:
    case BodySlideZ:
        return "What a dragging surface does to the fluid touching it. The body itself does not move: rot spins the surface about its own centre and rotX and rotY roll it about the other two axes, while slideX, slideY and slideZ run it along like a conveyor belt. All six add up.";
    case BodyVelocityX:
    case BodyVelocityY:
    case BodyVelocityZ:
    case BodySpin:
    case BodySpinX:
    case BodySpinY:
        return "How the body itself moves. Under travel this is the path; under free it is the velocity it is let go with, and the flow takes it from there. The two rolls only mean anything in a volume: in one plane there is nothing for the body to tip into.";
    case BodyMass:
    case BodyDensity:
        return "What a free body weighs: per metre of depth while the run is one plane, and kilograms outright once nz is above 1. Give it a mass, or a density and let the area - or the volume - do the arithmetic, whichever you set last wins. A body much lighter than the fluid it displaces wants coupling = strong.";
    case BodyInertiaX:
    case BodyInertiaY:
        return "How hard the body is to roll about x and about y, the two turns the third dimension added. Left at zero the solver takes m*r^2/2 from the shape, which is what a disc of that rim has. The turn about z is the one the 2D runs always had and the solver still works that one out itself.";
    case BodyPins:
        return "Degrees of freedom held still while the rest are free. A cylinder free to spin but not to drift is x and y pinned; a body free to fall straight down is spin pinned.";
    case BodyPinZ:
    case BodyPinRotX:
    case BodyPinRotY:
        return "The three degrees of freedom the third dimension added, each held or not. Pinning all three leaves a body that moves exactly as it did in the plane, which is what a volume run reproducing a 2D one wants.";
    case BodyTrackLine:
        return "The keyframed poses the Layout view writes: one @t=..,x=..,y=..,z=..,rot=..,interp=..,ease=.. block per keyframe, per object. It is the UI's own record, it never reaches the solver, and every time it changes the Body motion row above is rewritten from it. Editing Body motion by hand still works; it just means the Layout view no longer knows where the body is meant to be.";
    case BodyPathKind:
        return "curve is the default: the control points are read as a Catmull-Rom spline and the body follows the smooth path through all of them, which is what a body flying an arc actually does. The bodyMotion that goes out is that curve sampled into short legs, so the solver still sees nothing but keyframes. keys is the older way - one straight leg from each pose to the next, and a corner at every one of them.";
    case BodyMotionLine:
        return "bodyMotion, in the solver's own grammar: <object>:vx=0.2,omega=45;<object>:free=1,mass=2. The rows above write into this and it is what is sent, so editing it by hand does the same thing. vz=, omegaX= and omegaY= carry the body out of the plane, pinZ=, pinRotX= and pinRotY= hold those three still, and inertiaX= and inertiaY= are the two rolls' resistance. @<seconds> opens a keyframe, which the rows above cannot write.";
    case BodyCollisions:
        return "Off, bodies pass straight through each other and through the walls - which is what every run before this did and is fine while nothing can meet anything. On, a body that would run into another one or into the domain edge bounces instead. Contact is read off the mask, so it is exact for any shape and not a circle around it.";
    case BodyRestitution:
        return "How much of the closing speed survives a bounce. 0 is a body that hits and stops dead, 1 is one that comes back at the speed it arrived. A body whose path you set is unmovable by construction, so a free body hitting one takes the whole impulse.";
    case BodyForceReport:
        return "Work out the fluid force on bodies whose path you set as well. It never changes where they go - a set path is a set path - it only puts the force in the step line so you can read what the fluid was doing to it. Free bodies always have it computed, because that is what moves them.";
    case BodyCouplingKind:
        return "How a free body is coupled to the fluid. added carries the fluid that moves with the body on the left hand side of its own equation of motion and costs nothing. strong iterates force and motion inside the step until they agree and costs that many pressure solves. weak does neither and is here to be compared against - it goes unstable as soon as the body is not much heavier than what it displaces.";
    case WallMotionLine:
        return "wallMotion, in the solver's own grammar: <object>:rot=90,slideX=0.5;<object>:slip=1. The object numbers are the ones the solver prints for the mask. rotX= and rotY= roll the surface about the other two axes and slideZ= runs it along +z; rot= is another way of writing rotZ=.";
    case Convection:
        return "upwind is first order and what this solver has always used. muscl is second order where the field is smooth, limited where it is not. central does no limiting at all.";
    case Limiter:
        return "Which limiter muscl uses. minmod is the most diffusive and the safest, superbee the least of both.";
    case TimeSchemeKind:
        return "euler is one projection a step. rk2 and rk3 project on every stage, cost that many times more and are what anything other than upwind needs under it.";
    case RunName:
        return "A name for this run. Empty gives the old run-<timestamp> folder; anything else becomes the folder name, and appears in the tray and the title while it runs.";
    case VortexLine:
        return "x=0.5,y=0.5,z=0.5,radius=0.05,strength=60,axis=z - the axis passes through that point, the swirl is fastest at that radius and that is how fast it is there, in m/s. Semicolons between several. Compressible runs get the isentropic vortex, which is an exact solution of the Euler equations and is carried along by the stream without changing shape. Keep the radius to eight cells or more or the scheme smears it away.";
    case ExtraFields:
        return "Extra fields written into every frame for ParaView and the results view: vorticity, divergence, speed, objectId, comma separated.";
    case DomainX:
    case DomainY:
    case DomainZ:
        return "Physical domain size. dx=Lx/nx, dy=Ly/ny and dz=Lz/nz.";
    case CellsX:
    case CellsY:
        return "Grid resolution. Typed values may exceed the slider range, subject to validation.";
    case CellsZ:
        return "nz: how many cells deep the box is. 1 is one plane, the way every earlier version ran, and everything about such a run - the fields, the frames, the forces per metre of depth - is what it always was. Above 1 the run is a volume and costs nz times as much per step, so the third dimension is never taken by accident.";
    case Cfl:
        return "CFL controls the advective timestep restriction; valid range is (0, 1].";
    case TotalTime:
        return "Simulation duration in seconds for a new run.";
    case AddTime:
        return "Continuation only: seconds to add to the time the chosen frame stopped at. Zero uses Total time as the target instead.";
    case DtUpdateInterval:
        return "How many solver steps elapse between adaptive dt recalculations.";
    case DtSafety:
        return "Safety multiplier applied to the timestep; valid range is (0, 1].";
    case CoarseSorOmega:
        return "Relaxation omega for the coarsest pressure solve; must be in (0, 2).";
    case SmootherOmega:
        return "Relaxation omega used by multigrid smoothing; must be in (0, 2).";
    case MgIterations:
        return "Maximum/target multigrid V-cycles per pressure solve.";
    case MgTolerance:
        return "Relative residual tolerance for the pressure solve.";
    case MgMinCoarseSize:
        return "Minimum coarse-grid size used when building the multigrid hierarchy.";
    case SaveInterval:
        return "Write one VTK result every N solver steps.";
    case UseCuda:
        return "Requests CUDA in a CUDA-capable build; CPU-only builds force this off.";
    case UseAvx2:
        return "Lets the solver use its AVX2 kernels. Off falls back to the scalar path, which is slower and agrees to solver tolerance. Needs Fluid Solver 0.2 or newer.";
    case UseOpenMp:
        return "Lets the solver spread its loops over every core. Needs Fluid Solver 0.2 or newer.";
    case SolverThreads:
        return "How many threads the solver may use. 0 means every core. Needs Fluid Solver 0.2 or newer.";
    case CacheMegabytes:
        return "Maximum decoded VTK frame cache owned by the UI, in MiB.";
    default:
        return {};
    }
}

} // namespace maskui
