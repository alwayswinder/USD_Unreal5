from pxr import Usd, UsdGeom, Sdf
import unreal
import os
from usd_unreal import exporting_utils
from usd_unreal import level_exporter
from usd_unreal.constants import *

class UsdExtraExportContext:
    def __init__(self):
        self.options = unreal.USDExtraExportOptions()

        self.world = None                   # UWorld object that is actually being exported (likely the editor world, but can be others when exporting Level assets)
        self.stage = None                   # Opened Usd.Stage for the main scene

        self.root_layer_path = ""           # Full, absolute filepath of the main scene root layer to export
        self.scene_file_extension = ""      # ".usda", ".usd" or ".usdc". Extension to use for scene files, asset files, material override files, etc.

        self.level_to_sublayer = {}         # Maps from unreal.Level to corresponding Sdf.Layer
        self.materials_sublayer = None      # Dedicated layer to author material overrides in

        self.exported_prim_paths = set()    # Set of already used prim paths e.g. "/Root/MyPrim"
        self.exported_assets = {}           # Map from unreal.Object to exported asset path e.g. "C:/MyFolder/Assets/Cube.usda"
        self.baked_materials = {}           # Map from unreal.Object.get_path_name() to exported baked filepath e.g. "C:/MyFolder/Assets/Red.usda"


class UsdExtraConversionContext:
    """ RAII-style wrapper around unreal.UsdConversionContext, so that Cleanup is always called.

    Usage:
    with UsdConversionContext("C:/MyFolder/RootLayer.usda") as converter:
        converter.ConvertMeshComponent(component, "/MyMeshPrim")
    """

    def __init__(self, root_layer_path, world):
        self.root_layer_path = root_layer_path
        self.world = world
        self.context = None

    def __enter__(self):
        self.context = unreal.UsdExtraConversionContext()
        self.context.set_stage_root_layer(unreal.FilePath(self.root_layer_path))
        self.context.world = self.world
        return self.context

    def __exit__(self, type, value, traceback):
        self.context.cleanup()


def get_schema_name_for_component(component):
    """ Uses a priority list to figure out what is the best schema for a prim matching `component`

    Matches UsdUtils::GetComponentTypeForPrim() from the USDImporter C++ source.

    :param component: unreal.SceneComponent object or derived
    :returns: String containing the intended schema for a prim matching `component` e.g. "UsdGeomMesh"
    """

    owner_actor = component.get_owner()
    if isinstance(owner_actor, unreal.InstancedFoliageActor):
        return 'PointInstancer'
    elif isinstance(owner_actor, unreal.LandscapeProxy):
        return 'Mesh'

    if isinstance(component, unreal.SkinnedMeshComponent):
        return 'SkelRoot'
    elif isinstance(component, unreal.HierarchicalInstancedStaticMeshComponent):
        # The original HISM component becomes just a regular Xform prim, so that we can handle
        # its children correctly. We'll manually create a new child PointInstancer prim to it
        # however, and convert the HISM data onto that prim.
        # c.f. convert_component()
        return 'Xform'
    elif isinstance(component, unreal.StaticMeshComponent):
        return 'Mesh'
    elif isinstance(component, unreal.BrushComponent):
        return 'Mesh'
    elif isinstance(component, unreal.CineCameraComponent):
        return 'Camera'
    elif isinstance(component, unreal.DirectionalLightComponent):
        return 'DistantLight'
    elif isinstance(component, unreal.RectLightComponent):
        return 'RectLight'
    elif isinstance(component, unreal.PointLightComponent):  # SpotLightComponent derives PointLightComponent
        return 'SphereLight'
    elif isinstance(component, unreal.SkyLightComponent):
        return 'DomeLight'
    elif isinstance(component, unreal.SceneComponent):
        return 'Xform'
    return ""


def should_export_actor(actor):
    """ Heuristic used to decide whether the received unreal.Actor should be exported or not

    :param actor: unreal.Actor to check
    :returns: True if the actor should be exported
    """
    actor_classes_to_ignore = {unreal.AbstractNavData,
                               unreal.AtmosphericFog,
                               unreal.DefaultPhysicsVolume,
                               unreal.GameModeBase,
                               unreal.GameNetworkManager,
                               unreal.GameplayDebuggerCategoryReplicator,
                               unreal.GameplayDebuggerPlayerManager,
                               unreal.GameSession,
                               unreal.GameStateBase,
                               unreal.HUD,
                               unreal.LevelSequenceActor,
                               unreal.ParticleEventManager,
                               unreal.PlayerCameraManager,
                               unreal.PlayerController,
                               unreal.PlayerStart,
                               unreal.PlayerState,
                               unreal.SphereReflectionCapture,
                               unreal.USDLevelInfo,
                               unreal.UsdStageActor,
                               unreal.WorldSettings}
    actor_class_names_to_ignore = {'BP_Sky_Sphere_C'}
    actor_instance_names_to_ignore = {"Brush_1", "DefaultPhysicsVolume_0"}

    # The editor world's persistent level always has a foliage actor, but it may be empty/unused
    if isinstance(actor, unreal.InstancedFoliageActor):
        foliage_types = actor.get_used_foliage_types()
        for foliage_type in foliage_types:
            transforms = actor.get_instance_transforms(foliage_type, actor.get_outer())
            if len(transforms) > 0:
                return True
        return False

    # This is a tag added to all actors spawned by the UsdStageActor
    if actor.actor_has_tag("SequencerActor"):
        return False

    for unreal_class in actor_classes_to_ignore:
        if isinstance(actor, unreal_class):
            return False

    unreal_class_name = actor.get_class().get_name()
    if unreal_class_name in actor_class_names_to_ignore:
        return False

    unreal_instance_name = actor.get_name()
    if unreal_instance_name in actor_instance_names_to_ignore:
        return False

    return True


def export_mesh(context, mesh, mesh_type):
    """ Exports a single Static/Skeletal mesh

    :param context: UsdExportContext object describing the export
    :param mesh: unreal.SkeletalMesh or unreal.StaticMesh object to export
    :param mesh_type: String 'static' or 'skeletal', according to the types of `meshes`
    :returns: (Bool, String) containing True if the export was successful, and the output filename that was used
    """
    mesh_file = level_exporter.get_filename_to_export_to(os.path.dirname(context.root_layer_path), mesh, context.scene_file_extension)

    options = None
    if mesh_type == 'static':
        options = unreal.StaticMeshExporterUSDOptions()
        options.stage_options = context.options.stage_options
        options.use_payload = context.options.use_payload
        options.payload_format = context.options.payload_format
    elif mesh_type == 'skeletal':
        options = unreal.SkeletalMeshExporterUSDOptions()
        options.inner.stage_options = context.options.stage_options
        options.inner.use_payload = context.options.use_payload
        options.inner.payload_format = context.options.payload_format

    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = mesh_file
    task.selected = False
    task.replace_identical = True
    task.prompt = False
    task.automated = True
    task.options = options

    unreal.log(f"Exporting {mesh_type} mesh '{mesh.get_name()}' to filepath '{mesh_file}'")
    success = unreal.Exporter.run_asset_export_task(task)

    if not success:
        unreal.log_warning(f"Failed to export {mesh_type} mesh '{mesh.get_name()}' to filepath '{mesh_file}'")
        return (False, mesh_file)

    # Add USD info to exported asset
    try:
        stage = Usd.Stage.Open(mesh_file)
        usd_prim = stage.GetDefaultPrim()
        attr = usd_prim.CreateAttribute('unrealAssetReference', Sdf.ValueTypeNames.String)
        attr.Set(mesh.get_path_name())
        model = Usd.ModelAPI(usd_prim)
        # model.SetAssetIdentifier(mesh_file)
        model.SetAssetName(mesh.get_name())

        # Take over material baking for the mesh, so that it's easier to share
        # baked materials between mesh exports
        if context.options.bake_materials:
            level_exporter.replace_unreal_materials_with_baked(
                stage,
                stage.GetRootLayer(),
                context.baked_materials,
                is_asset_layer=True,
                use_payload=context.options.use_payload,
                remove_unreal_materials=context.options.remove_unreal_materials
            )

        stage.Save()
    except BaseException as e:
        if len(mesh_file) > 220:
            unreal.log_error(f"USD failed to open a stage with a very long filepath: Try to use a destination folder with a shorter file path")
        unreal.log_error(e)

    return (True, mesh_file)


def export_meshes(context, meshes, mesh_type):
    """ Exports a collection of Static/Skeletal meshes

    :param context: UsdExportContext object describing the export
    :param meshes: Homogeneous collection of unreal.SkeletalMesh or unreal.StaticMesh
    :param mesh_type: String 'static' or 'skeletal', according to the types of `meshes`
    :returns: None
    """
    num_meshes = len(meshes)
    unreal.log(f"Exporting {num_meshes} {mesh_type} meshes")

    with unreal.ScopedSlowTask(num_meshes, f"Exporting {mesh_type} meshes") as slow_task:
        slow_task.make_dialog(True)

        for mesh in meshes:
            if slow_task.should_cancel():
                break
            slow_task.enter_progress_frame(1)

            success, path = export_mesh(context, mesh, mesh_type)
            if success:
                context.exported_assets[mesh] = path


def collect_actors(context):
    """ Collects a list of actors to export according to `context`'s options

    :param context: UsdExportContext object describing the export
    :returns: Set of unreal.Actor objects that were collected
    """
    actors = None
    if context.options.selection_only:
        # Go through LayersSubsystem as EditorLevelLibrary has too aggressive filtering
        layers_subsystem = unreal.LayersSubsystem()
        actors = set(layers_subsystem.get_selected_actors())
    else:
        actors = unreal.UsdConversionLibrary.get_actors_to_convert(context.world)
    actors = set([a for a in actors if a])

    # Each sublevel has an individual (mostly unused) world, that actually has the name of the sublevel
    # on the UE editor. The levels themselves are all named "Persistent Level", so we can't filter based on them
    if len(context.options.levels_to_ignore) > 0:
        persistent_level_allowed = "Persistent Level" not in context.options.levels_to_ignore

        filtered_actors = set()
        for actor in actors:
            # If the actor is in a sublevel, this will return the persistent world instead
            persistent_world = actor.get_world()

            # If the actor is in a sublevel, this will be the (mostly unused) world that actually owns the sublevel
            actual_world = actor.get_outer().get_outer()

            actor_in_persistent_level = actual_world == persistent_world
            sublevel_name = actual_world.get_name()

            # Have to make sure we only allow it via sublevel name if the actor is not on the persistent level,
            # because if it is then the level name will be the name of the ULevel asset (and not actually "Persistent Level")
            # and we'd incorrectly let it through
            if (persistent_level_allowed and actor_in_persistent_level) or (not actor_in_persistent_level and sublevel_name not in context.options.levels_to_ignore):
                filtered_actors.add(actor)

        actors = set(filtered_actors)

    actors = [a for a in actors if should_export_actor(a)]
    return actors


def collect_actors_in_folder(actors, folder_name):
    actors_in_folder = set()

    for actor in actors:
        if actor.get_folder_path() == folder_name:
            actors_in_folder.add(actor)

    return actors_in_folder


def convert_folder(context, folder_names):
    for folder_name in folder_names:
        folder_path_str = unreal.StringLibrary.conv_name_to_string(folder_name)
        prim_name = ""
        folder_name_tail = ""
        if folder_path_str:
            segments_original = folder_path_str.split('/')
            folder_name_tail = segments_original[len(segments_original)-1]
            segments_valid = [exporting_utils.Tf.MakeValidIdentifier(seg) for seg in folder_path_str.split('/')]
            prim_name = "/".join(segments_valid)
            unreal.log(f"Exporting Folder '{prim_name}'")
            prim = context.stage.DefinePrim('/' + ROOT_PRIM_NAME + '/' + prim_name, 'Scope')
            path_attr = prim.CreateAttribute('unrealActorFolderPath', Sdf.ValueTypeNames.String)
            path_attr.Set(folder_name_tail)
            usage_attr = prim.CreateAttribute('unrealPrimUsage', Sdf.ValueTypeNames.Token)
            usage_attr.Set('folder')


def get_prim_path_for_component(component, used_prim_paths, parent_prim_path="", use_folders=False):
    """ Finds a valid, full and unique prim path for a prim corresponding to `component`

    For a level with an actor named `Actor`, with a root component `SceneComponent0` and an attach child
    static mesh component `mesh`, this will emit the path "/<ROOT_PRIM_NAME>/Actor/SceneComponent0/mesh"
    when `mesh` is the `component` argument.

    If provided a parent_prim_path (like "/<ROOT_PRIM_NAME>/Actor/SceneComponent0"), it will just append
    a name to that path. Otherwise it will traverse up the attachment hierarchy to discover the full path.

    :param component: unreal.SceneComponent object to fetch a prim path for
    :param used_prim_paths: Collection of prim paths we're not allowed to use e.g. {"/Root/OtherPrim"}.
                            This function will append the generated prim path to this collection before returning
    :param parent_prim_path: Path of the parent prim to optimize traversal with e.g. "/Root"
    :returns: Valid, full and unique prim path str e.g. "/Root/Parent/ComponentName"
    """
    name = component.get_name()

    # Use actor name if we're a root component, and also keep track of folder path in that case too
    actor = component.get_owner()
    folder_path_str = ""
    if actor:
        if actor.get_editor_property('root_component') == component:
            name = actor.get_actor_label()
            if use_folders:
                if component.get_attach_parent() is None:
                    folder_path = actor.get_folder_path()
                    if not folder_path.is_none():
                        folder_path_str = str(folder_path)

    # Make name valid for USD
    name = exporting_utils.Tf.MakeValidIdentifier(name)

    # If we have a folder path, sanitize each folder name separately or else we lose the slashes in case of nested folders
    # Defining a prim with slashes in its name like this will lead to parent prims being constructed for each segment, if necessary
    if folder_path_str:
        segments = [exporting_utils.Tf.MakeValidIdentifier(seg) for seg in folder_path_str.split('/')]
        segments.append(name)
        name = "/".join(segments)

    # Find out our parent prim path if we need to
    if parent_prim_path == "":
        parent_comp = component.get_attach_parent()
        if parent_comp is None:  # Root component of top-level actor -> Top-level prim
            parent_prim_path = "/" + ROOT_PRIM_NAME
        else:
            parent_prim_path = get_prim_path_for_component(
                parent_comp,
                used_prim_paths,
                use_folders=use_folders
            )
    path = parent_prim_path + "/" + name

    path = exporting_utils.get_unique_name(used_prim_paths, path)
    used_prim_paths.add(path)

    return path


def convert_component(context, converter, component, visited_components=set(), parent_prim_path=""):
    """ Exports a component onto the stage, creating the required prim of the corresponding schema as necessary

    :param context: UsdExportContext object describing the export
    :param component: unreal.SceneComponent to export
    :param visited_components: Set of components to skip during traversal. Exported components are added to it
    :param parent_prim_path: Prim path the parent component was exported to (e.g. "/Root/Parent").
                             Purely an optimization: Can be left empty, and will be discovered automatically
    :returns: None
    """
    actor = component.get_owner()
    if not should_export_actor(actor):
        return

    # We use this as a proxy for bIsVisualizationComponent
    if component.is_editor_only:
        return

    prim_path = get_prim_path_for_component(
        component,
        context.exported_prim_paths,
        parent_prim_path,
        context.options.export_actor_folders
    )

    unreal.log(f"Exporting component '{component.get_name()}' onto prim '{prim_path}'")

    prim = context.stage.GetPrimAtPath(prim_path)
    if not prim:
        target_schema_name = get_schema_name_for_component(component)
        unreal.log(f"Creating new prim at '{prim_path}' with schema '{target_schema_name}'")
        prim = context.stage.DefinePrim(prim_path, target_schema_name)

    ## Add attributes to prim depending on component type
    if isinstance(component, unreal.HierarchicalInstancedStaticMeshComponent):
        # Create a separate new child of `prim` that can receive our PointInstancer component schema instead.
        # We do this because we can have any component tree in UE, but in USD the recommendation is that you don't
        # place drawable prims inside PointInstancers, and that DCCs don't traverse PointInstancers looking for drawable
        # prims, so that they can work as a way to "hide" their prototypes
        child_prim_path = exporting_utils.get_unique_name(context.exported_prim_paths, prim_path + "/HISMInstance")
        context.exported_prim_paths.add(child_prim_path)

        unreal.log(f"Creating new prim at '{child_prim_path}' with schema 'PointInstancer'")
        child_prim = context.stage.DefinePrim(child_prim_path, "PointInstancer")

        level_exporter.assign_hism_component_assets(component, child_prim, context.exported_assets)
        converter.convert_hism_component(component, child_prim_path)

    if isinstance(component, unreal.StaticMeshComponent):
        level_exporter.assign_static_mesh_component_assets(component, prim, context.exported_assets)
        converter.convert_mesh_component(component, prim_path)

    elif isinstance(component, unreal.SkinnedMeshComponent):
        level_exporter.assign_skinned_mesh_component_assets(component, prim, context.exported_assets)
        converter.convert_mesh_component(component, prim_path)
#
    #elif isinstance(component, unreal.CineCameraComponent):
#
    #    # If we're the main camera component of an ACineCameraActor, then write that out on our parent prim
    #    # so that if we ever import this file back into UE we can try to reconstruct the ACineCameraActor
    #    # with the same root and camera components, instead of creating new ones
    #    owner_actor = component.get_owner()
    #    if isinstance(owner_actor, unreal.CineCameraActor):
    #        main_camera_component = owner_actor.get_editor_property("camera_component")
    #        if component == main_camera_component:
    #            parent_prim = prim.GetParent()
    #            if parent_prim:
    #                attr = parent_prim.CreateAttribute('unrealCameraPrimName', Sdf.ValueTypeNames.Token)
    #                attr.Set(prim.GetName())
#
    #    converter.convert_cine_camera_component(component, prim_path)
#
    #elif isinstance(component, unreal.LightComponentBase):
    #    converter.convert_light_component(component, prim_path)
#
    #    if isinstance(component, unreal.SkyLightComponent):
    #        converter.convert_sky_light_component(component, prim_path)
#
    #    if isinstance(component, unreal.DirectionalLightComponent):
    #        converter.convert_directional_light_component(component, prim_path)
#
    #    if isinstance(component, unreal.RectLightComponent):
    #        converter.convert_rect_light_component(component, prim_path)
#
    #    if isinstance(component, unreal.PointLightComponent):
    #        converter.convert_point_light_component(component, prim_path)
#
    #        if isinstance(component, unreal.SpotLightComponent):
    #            converter.convert_spot_light_component(component, prim_path)
#
    if isinstance(component, unreal.BrushComponent):
        converter.convert_brush_component(component, prim_path)
        unreal.log_warning(f"Convert Brush Component: '{component.get_name()}'")

    if isinstance(component, unreal.SceneComponent):
        converter.convert_scene_component(component, prim_path)

        owner_actor = component.get_owner()

        # We have to export the instanced foliage actor in one go, because it will contain one component
        # for each foliage type, and we don't want to end up with one PointInstancer prim for each
        if isinstance(owner_actor, unreal.InstancedFoliageActor):
            level_exporter.assign_instanced_foliage_actor_assets(actor, prim, context.exported_assets)
            converter.convert_instanced_foliage_actor(actor, prim_path)
        elif isinstance(owner_actor, unreal.LandscapeProxy):
            success, mesh_path = level_exporter.export_landscape(context, owner_actor)
            if success:
                level_exporter.add_relative_reference(prim, mesh_path)
            else:
                unreal.log_warning(f"Failed to export landscape '{owner_actor.get_name()}' to filepath '{mesh_path}'")
        else:
            # Recurse to children
            for child in component.get_children_components(include_all_descendants=False):
                if child in visited_components:
                    continue
                visited_components.add(child)

                convert_component(context, converter, child, visited_components, prim_path)


def export_actors(context, actors):
    """ Will export the `actors`' component hierarchies as prims on the context's stage

    :param context: UsdExportContext object describing the export
    :param actors: Collection of unreal.Actor objects to iterate over and export
    :returns: None
    """
    unreal.log(f"Exporting components from {len(actors)} actors")

    visited_components = set()

    with UsdExtraConversionContext(context.root_layer_path, context.world) as converter:
        if context.options.export_actor_folders:
            folder_names = converter.get_world_folders_names();
            convert_folder(context, folder_names)

        # Component traversal ensures we parse parent components before child components,
        # but since we get our actors in random order we need to manually ensure we parse
        # parent actors before child actors. Otherwise we may parse a child, end up having USD create
        # all the parent prims with default Xform schemas to make the child path work, and then
        # not being able to convert a parent prim to a Mesh schema (or some other) because a
        # default prim already exists with that name, and it's a different schema.
        def attach_depth(a):
            depth = 0
            parent = a.get_attach_parent_actor()
            while parent:
                depth += 1
                parent = parent.get_attach_parent_actor()
            return depth
        actor_list = list(actors)
        actor_list.sort(key=attach_depth)

        for actor in actor_list:
            comp = actor.get_editor_property("root_component")
            if not comp or comp in visited_components:
                continue
            visited_components.add(comp)

            # If this actor is in a sublevel, make sure the prims are authored in the matching sub-layer
            with exporting_utils.ScopedObjectEditTarget(actor, context):
                this_edit_target = context.stage.GetEditTarget().GetLayer().realPath
                converter.set_edit_target(unreal.FilePath(this_edit_target))

                convert_component(context, converter, comp, visited_components)


def export_level(context, actors):
    """ Exports the actors and components of the level to the main output root layer, and potentially sublayers

    This function creates the root layer for the export, and then iterates the component attachment hierarchy of the
    current level, creating a prim for each exportable component.

    :param context: UsdExportContext object describing the export
    :returns: None
    """
    unreal.log(f"Creating new stage with root layer '{context.root_layer_path}'")

    with unreal.ScopedSlowTask(3, f"Exporting main root layer") as slow_task:
        slow_task.make_dialog(True)

        slow_task.enter_progress_frame(1)

        # Setup stage
        context.stage = Usd.Stage.CreateNew(context.root_layer_path)
        root_prim = context.stage.DefinePrim('/' + ROOT_PRIM_NAME, 'Xform')
        context.stage.SetDefaultPrim(root_prim)
        root_layer = context.stage.GetRootLayer()

        # Set stage metadata
        UsdGeom.SetStageUpAxis(context.stage, UsdGeom.Tokens.z if context.options.stage_options.up_axis == unreal.UsdUpAxis.Z_AXIS else UsdGeom.Tokens.y)
        UsdGeom.SetStageMetersPerUnit(context.stage, context.options.stage_options.meters_per_unit)
        context.stage.SetStartTimeCode(context.options.start_time_code)
        context.stage.SetEndTimeCode(context.options.end_time_code)

        # Prepare sublayers for export, if applicable
        if context.options.export_sublayers:
            levels = set([a.get_outer() for a in actors])
            context.level_to_sublayer = exporting_utils.create_a_sublayer_for_each_level(context, levels)
            if context.options.bake_materials:
                context.materials_sublayer = level_exporter.setup_material_override_layer(context)

        # Export actors
        export_actors(context, actors)

        # Post-processing
        slow_task.enter_progress_frame(1)
        if context.options.bake_materials:
            override_layer = context.materials_sublayer if context.materials_sublayer else root_layer
            level_exporter.replace_unreal_materials_with_baked(
                context.stage,
                override_layer,
                context.baked_materials,
                is_asset_layer=False,
                use_payload=context.options.use_payload,
                remove_unreal_materials=context.options.remove_unreal_materials
            )

            # Abandon the material overrides sublayer if we don't have any material overrides
            if context.materials_sublayer and context.materials_sublayer.empty:
                # Cache this path because the materials sublayer may get suddenly dropped as we erase all sublayer references
                materials_sublayer_path = context.materials_sublayer.realPath
                sublayers = context.stage.GetLayerStack(includeSessionLayers=False)
                sublayers.remove(context.materials_sublayer)

                for sublayer in sublayers:
                    relative_path = unreal.UsdConversionLibrary.make_path_relative_to_layer(sublayer.realPath, materials_sublayer_path)
                    sublayer.subLayerPaths.remove(relative_path)
                os.remove(materials_sublayer_path)

        # Write file
        slow_task.enter_progress_frame(1)
        unreal.log(f"Saving root layer '{context.root_layer_path}'")
        if context.options.export_sublayers:
            # Use the layer stack directly so we also get a material sublayer if we made one
            for sublayer in context.stage.GetLayerStack(includeSessionLayers=False):
                sublayer.Save()
        context.stage.GetRootLayer().Save()


def export(context):
    """ Exports the current level according to the received export context

    :param context: UsdExportContext object describing the export
    :returns: None
    """
    if not context.world or not isinstance(context.world, unreal.World):
        unreal.log_error("UsdExportContext's 'world' member must point to a valid unreal.World object!")
        return

    context.root_layer_path = exporting_utils.sanitize_filepath(context.root_layer_path)

    if not context.scene_file_extension:
        extension = os.path.splitext(context.root_layer_path)[1]
        context.scene_file_extension = extension if extension else ".usda"
    if not context.options.payload_format:
        context.options.payload_format = context.scene_file_extension

    unreal.log(f"Starting export to root layer: '{context.root_layer_path}'")

    with unreal.ScopedSlowTask(4, f"Exporting level to '{context.root_layer_path}'") as slow_task:
        slow_task.make_dialog(True)

        # Collect items to export
        slow_task.enter_progress_frame(1)
        actors = collect_actors(context)

        static_meshes = level_exporter.collect_static_meshes(actors)
        skeletal_meshes = level_exporter.collect_skeletal_meshes(actors)
       # materials = level_exporter.collect_materials(actors, static_meshes, skeletal_meshes)
#
        # Export assets
       # slow_task.enter_progress_frame(1)
       # export_materials(context, materials)
#
        slow_task.enter_progress_frame(1)
        export_meshes(context, static_meshes, 'static')
#
        slow_task.enter_progress_frame(1)
        export_meshes(context, skeletal_meshes, 'skeletal')
#
        # Export actors
        slow_task.enter_progress_frame(1)
        export_level(context, actors)


def export_with_cdo_options():
    options_cdo = unreal.get_default_object(unreal.USDExtraExportOptions)
    context = UsdExtraExportContext()
    context.root_layer_path = options_cdo.file_name
    context.world = options_cdo.world
    context.options = options_cdo

    export(context)
