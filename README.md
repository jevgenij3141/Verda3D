# Verda 3D

This editor was created entirely with AI. The author described the features in natural language and used AI to write the code without programming it by hand.

Verda 3D is a Win32 model editor for Windows. It builds with CMake 3.20 or later and a C++20 compiler. Raster graphics support native OpenGL and Vulkan through Mesa Zink.

A short startup animation shows a ray-traced ruby and other faceted crystals against clouds and mist, with a metallic title and the application version. Click the splash screen to dismiss it.

## Build and run

With MinGW available on your `PATH`:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\3D1Editor.exe
```

On the first build, CMake downloads Mesa 25.1.9, verifies its SHA-256 checksum, and places the Vulkan runtime in the `vulkan` directory next to the executable. Pass `-DTHREED1_BUNDLE_VULKAN=OFF` to build without the bundled runtime. [Mesa Zink documentation](https://docs.mesa3d.org/drivers/zink.html)

**Viewport → Configure…** switches between **Using OpenGL** and **Using Vulkan**. The selection takes effect after restarting the editor and applies to viewports and raster image and video rendering. The editor probes the GPU's Vulkan driver at startup and falls back to software Vulkan through Lavapipe if the probe fails. If both Vulkan options are unavailable, the editor reports the failure and starts with OpenGL. The configuration dialog shows the active backend. **Global Illumination** runs on the CPU with either backend.

## Viewports and object tools

- The four viewports show perspective, top, front, and right views of the same scene.
- Use the menu on the left of each viewport to change its display mode. **Object** shows opaque models with textures, lighting, and shadows. **Wireframe** shows polygon edges, including back-facing edges. **Software** shows a raster preview with transparent materials. Each viewport stores its own display mode. Final render settings are independent.
- Click **+** in the left toolbar to add a **Box**, **Plane**, **Sphere**, **Cylinder**, **Pyramid**, **Camera**, or **Teapot**. The **Crystals** submenu adds pointed, octahedral, step-cut, and brilliant-cut meshes. New items appear in the **Scene** list.
- Select an object by clicking it in a viewport or clicking its name in **Scene**. Hold **Ctrl** while clicking to add or remove objects from the selection.
- Choose **Select** in the toolbar below the menu bar to pick objects or mesh components without moving them.
- Use **Objects → Group** to group the selection. Click the arrow beside the group name to reveal its children. You can move, rotate, and uniformly scale the group. **Objects → Ungroup** keeps the members in place. Groups are saved with the scene.
- To cut a shape out of a closed mesh, overlap it with a second closed mesh. Select the mesh to cut, **Ctrl + click** the cutter, and choose **Objects → Cut (Difference)** or the corresponding command under the hammer and saw icon. The cutter stays hidden in **Scene**. The operation bakes the target mesh's active modifiers into the result.
- Right-click a **Scene** row for object commands. **F2** renames, **Ctrl+D** duplicates, **Del** deletes, and **H** toggles visibility. **F** frames the object in the active viewport. **Transform…** below the list opens precise position, rotation, and scale controls.
- **Ctrl+Z** undoes a change. **Ctrl+Y** or **Ctrl+Shift+Z** redoes it. A completed drag or extrusion is one undo step. Press **Esc** during a drag to cancel it.
- Undo and Redo work inside vertex, edge, and polygon editing. They restore one mesh operation at a time while keeping the editor open. Press **Enter** to commit the edited mesh as one scene-level undo step.
- Use the third toolbar button or the **Edit** menu to switch between **Move**, **Scale**, and **Rotate**. **Move** arrows constrain movement to one axis. The squares between them constrain it to **XY**, **XZ**, or **YZ**. In **Scale**, the squares at the ends of the axes resize along one local axis, and the squares between the axes resize along two. In **Rotate**, the rings rotate around the world axes. Red is X, green is Y, and blue is Z.
- Hold **Alt** and drag with the left mouse button to orbit the viewport around its center while keeping the horizon level. This also works in vertex, edge, and polygon editing modes. Scroll to zoom toward the pointer and drag with the middle mouse button to pan. **Edit → Reset active view** restores the original view.
- Resize the main window by dragging an edge or the lower-right grip. Use the Windows maximize button to maximize it. **Full screen** or **F11** enters fullscreen. **F11**, **Esc**, or **Exit full screen** exits it.
- Bind a viewport to a camera through the **Camera** menu in its upper-right corner. You can also select a camera in **Scene** and use **Objects → Assign selected camera** or **Camera view** below the list. Choose **Default view** in the **Camera** menu to restore the standard view.

## Mesh editing

- Select an object and choose **Extrude** from the **Edit** menu or the hammer and saw menu. **Ctrl + click** adds or removes faces from the selection. Drag any selected face to extrude along the average normal, or drag the **X**, **Y**, or **Z** arrow at the center of the selection. A single face can be dragged directly. Adjacent selected faces extrude together, with walls along their outer boundary. Release the mouse to apply the extrusion or press **Esc** during the drag to cancel it.
- A **Plane** has **X** and **Y** fields in the right panel for 1 to 32 subdivisions per axis. The first successful extrusion converts the plane to a **Mesh**.
- **Bridge Faces**, under **Edit** or the hammer and saw icon, joins two separate faces on one mesh. Click both faces and press **Enter**. The tool removes the selected faces and adds faces between their boundaries.
- The dot, line, and square buttons in the geometry properties switch to vertex, edge, and polygon editing. In **Move**, drag the selected component with the **X**, **Y**, or **Z** arrows, the **XY**, **XZ**, or **YZ** squares, or directly in the view plane. **Scale** uses the same axis and plane controls for edges and polygons and scales around the selection center. Press **Enter** to apply the edits or **Esc** to cancel them. An edited plane keeps its mesh, and its subdivision fields are hidden.
- To rotate an edge or polygon, switch to **Rotate** in the left toolbar or through **Edit → Rotate**. The rings rotate the selected component around its center. Choose **Move** to bring back the arrows and squares.
- **Ctrl + click** adds or removes vertices, edges, or polygons from the current selection. Switching between **Move**, **Rotate**, and **Scale** keeps the selection and its pivot. Rotate and Scale need at least two selected vertices.
- Drag from empty space to draw a selection box, or hold **Shift** and drag over geometry. Vertices inside the box and edges or faces that overlap it are selected, including components behind the visible surface. Hold **Ctrl** while drawing the box to add to the selection.
- In polygon editing, switch to **Extrude** to use the selected faces, then switch back to Move, Rotate, or Scale to work on the resulting caps. **Enter** applies all pending mesh edits and **Esc** discards them. If you switch from the standalone Extrude tool while faces are selected, their selection carries over into polygon editing.
- Right-click geometry in **Scene** for **Convert to Mesh**, **Shade Smooth**, and **Shade Flat**. Converting bakes the current shape with its active modifiers. **Shade Smooth** averages normals across neighboring faces. **Shade Flat** uses each face's own normal. Both preserve the polygon count.
- Select geometry and choose **Modifiers → Add Modifier → Smooth** in the right panel. Enable **Active** and set **Levels** from 1 to 4. The polygon count appears beside the heading. You can disable or remove the modifier. **Extrude** edits the base mesh, and final rendering uses the smoothed result.

## Materials and UVs

- **Materials → Scene Materials** opens the material swatches in a separate window. Click a sphere, select **Load texture...**, choose an image, and click **OK**. PNG, JPEG, BMP, GIF, and TIFF are supported.
- **Repeat X** and **Repeat Y** set how many times the texture repeats horizontally and vertically, from 1 to 256.
- Select geometry and choose **Edit Texture Mesh…** in the right panel. The window shows the UV layout over the object's texture. The list at the top lets you pick another project texture for that object. Drag the layout with **Move**, **Scale X**, **Scale Y**, or **Rotate**. **Ctrl + right-click** adds or removes a face from the selection. Repeated clicks in the same spot cycle through overlapping faces. With an empty selection, edits affect the entire layout. Scroll to zoom and drag with the middle mouse button to pan.
- **Edit → Restore** resets the current mesh's UV layout. **OK** applies the changes. **Cancel** or **Esc** discards them. UV coordinates and texture repeat settings are saved with the scene.
- **Edit Texture Mesh…** shows all six faces of a box in a cross when its faces use overlapping UVs. **Edit → Unfold** lays out mesh faces in separate parts of the texture. **Fit UVs** frames the entire current layout.
- **Smart Unfold** projects connected, gently curved faces as a single UV island where their projection fits without overlap. It splits at sharp bends and packs the islands onto the same texture with padding and a shared texel density.
- To assign a material, select the geometry, right-click a swatch, and choose **Apply**. Editing the swatch updates all objects that use it.
- The material editor previews changes on a sphere. It supports **Blinn-Phong**, **Phong**, **Lambert**, and **Unlit**, with controls for base color, ambient and diffuse lighting, specular strength and shininess, and emission. Base color tints the loaded texture.
- **Transmission** controls how much light passes through the material. **Refraction IOR** sets the index of refraction, and **Caustic strength** controls caustic intensity. Glass, Ocean, and Ice have preset values.
- **Materials → Library** opens the list of presets. Choosing a material adds it to **Scene Materials**, which scrolls with the scrollbar or mouse wheel. Ocean, Glass, and Ice support transparency in Software Rendering and reflection and refraction in Global Illumination.
- **Clear Water** and **River Water** have water refraction and different ripple strengths. **Ruby**, **Emerald**, **Sapphire**, **Amethyst**, and **Amber** use dielectric shading. Their previews show different crystal cuts in the library, the scene material shelf, and the material editor.

## Scene files and animation

- **File → Save Scene** (**Ctrl+S**) saves a **.3d1** file with geometry, lights, cameras, render settings, materials, and textures. **File → Open Scene** (**Ctrl+O**) loads a scene. **New Scene** (**Ctrl+N**) creates one. An asterisk in the title indicates unsaved changes.
- **File → Import OBJ** imports polygon meshes and UV coordinates from Wavefront OBJ. **Export OBJ** writes visible geometry and UVs. Use **.3d1** to preserve materials, cameras, and lights.
- On the **Timeline** below the viewports, select an object and use **Add key** or **K** to record its position, rotation, and scale. Move to another frame, transform the object, and add another key. Diamonds mark the keys, and the selected object's path appears in the viewports. **Shift+K** or **×** deletes the key on the current frame.
- The list beside **Add key** sets interpolation for new keys or changes the key on the current frame. **Linear** interpolates at a constant rate between two linear keys. **Smooth** eases in or out at the key. Interpolation types are saved with the scene.
- Timeline controls jump to the first, previous, next, or last frame and start or stop playback. **Space** toggles playback, **←/→** step by one frame, and **Home/End** jump to the first or last frame. The right side has controls for **FPS**, total frames, and the current frame. The **.3d1** file stores keys and timeline settings.

## Dynamics

- Raise a mesh above the grid, select it, and choose **Dynamics → Play** to preview a drop. If the scene already has Dynamic meshes, Play simulates those meshes. Otherwise, it simulates the selected Automatic meshes. Other Automatic meshes act as static obstacles.
- **Dynamics → Collision** sets the selected meshes' collision role, mass, bounce, friction, and initial velocity. It also sets scene gravity, the ground plane toggle, and ground height. Choose **Static** for an obstacle, **Dynamic** for a moving body, or **Ignore** to exclude a mesh.
- Preview collisions use world-aligned bounding boxes and solve linear motion. **Pause** freezes the preview, and **Reset** or **Esc** restores the starting poses. Editing or saving also resets the preview.
- **Bake to Timeline** adds transform keys from the current frame through the chosen last frame. Baked motion can be played, saved, rendered to video, or undone as a single edit.
- **Dynamics → Water into Dish** adds a shallow dish and an emitter positioned above it. Choose **Play** to see the stream fall into the dish. **Water Emitter** adds a source on its own. Move, scale, or rotate the emitter to change its position, nozzle width, or direction, and use **Emitter settings** to set emission rate, radius, initial speed, viscosity, lifetime, and surface quality.
- Water particles collide with mesh surfaces and the optional ground plane. The editor reconstructs a water mesh from the particles during playback. Pause and render the current frame, use **Keep Water Surface** to save that instant as a regular mesh with the Clear Water material, or use **Render to Video** to simulate water from the start of the export range. **Bake to Timeline** applies to rigid-body transforms.

## Lighting and rendering

- Choose **Effects → Fog** in the top toolbar to add a fog volume. Select the container or its row in **Scene** to change **Type** (Mist or Cloud), **Density**, **Noise scale**, **Flow**, and **Tint**. Move, rotate, or scale the container to position the effect. Viewports show the outline, Software Rendering composites a lightweight preview, and Global Illumination ray-marches the procedural volume with scene lighting. **Flow** animates the density pattern through the volume as the timeline advances. Transform keys animate the container itself. Fog settings are saved with the scene.
- Enable **Shadows** on a fog volume so dense areas block light from reaching deeper regions. Shadow-enabled lights also let geometry shade the volume. In Global Illumination, the volume can cast soft shadows on scene surfaces. Turning the option off keeps the fog's density and its opacity along the camera ray.
- **Cloud** uses a procedural cumulus density field with overlapping billows, a flatter base, and fine erosion along its edges. **Noise scale** changes the detail and **Flow** moves the pattern through the volume. Global Illumination samples the volume and its shadows more finely than Software Rendering.
- Choose **Fast**, **Balanced**, or **High** under **Quality** in the Fog properties. Higher quality increases the cloud density grid resolution and the number of volume and shadow samples. The first Global Illumination preview uses Fast quality while the final tiles use the selected setting.
- Use the second toolbar button to add a light. **Omni** shines in all directions, **Spot** casts a cone, and **Sky** lights the scene from a hemisphere. Select lights in the viewports or in **Scene**. Use **Rotate** to aim Spot and Sky lights.
- The selected light's properties appear below **Scene**. **Color…** shows and edits its color. **Intensity** sets its power. **Light settings** opens the controls for its light type. **Shadows** enables shadows, and **Bitmap** sets the shadow map resolution for **Software Rendering**. Use **…** for a custom size from 16 to 1024 pixels per side. **Shadow transparency** works in both rendering modes.
- Activate a viewport and choose **Rendering → Render** or press **F9** to render its view in a separate window, without the grid or handles. Run **Render** again to refresh it. In the render window, use **File → Save PNG** or **Ctrl+S** to export at the original render resolution.
- **Viewport → Viewport Settings → Show render frame** displays the render bounds for the active viewport. The shaded area outside the frame is cropped out. The setting is stored per viewport.
- In **Rendering → Setup Render**, choose a preset from VGA to 4K UHD or enter **Width** and **Height** from 64 to 4096 pixels. The scene saves these dimensions for still renders and PNG exports. Video export has separate dimensions.
- In **Rendering → Setup Render**, clear the current render mode to select the other one. **Software Rendering** uses the raster backend and shadow maps. **Global Illumination** uses CPU ray tracing for bounced light, color bleeding, and soft shadows. Set **Samples per pixel** from 1 to 65,536. More samples reduce noise and take longer to render. Closing the render window cancels the render.
- During a Global Illumination still render, a low-resolution preview appears first. Full-resolution tiles replace it from the center outward. The red bar below the image shows the percentage of final pixels rendered. **Save PNG** becomes available when the render finishes.
- Enable **Caustics** in **Rendering → Setup Render** to render focused light patterns from glass, water, and other transparent materials. Both modes support caustics in the final render. **Caustic photons** controls the number of rays used for the calculation. To try it, place a glass sphere above a plane, add an Omni or Spot light above the sphere, and reduce the light radius.
- **Rendering → Render to Video** exports **MP4 (H.264)**. Select a camera or the active viewport, start and end frames, FPS, width, height, and an output file. Both endpoints are included. Video dimensions must be even, with a maximum of 3840 × 2160 and up to 120 FPS.
- **Process** starts a background export with the settings from **Setup Render**. The progress bar is next to the button. The preview opens automatically. You can close it and reopen it with **Preview**. **Cancel job** stops the export. The output file is written after encoding finishes.
