from pxr import Usd, UsdGeom, Sdf
import unreal
import os
from usd_unreal import exporting_utils
from usd_unreal import level_exporter
from usd_unreal.constants import *
import usd_extra_export_scripts


def import_context(context):
    if not context.world or not isinstance(context.world, unreal.World):
        unreal.log_error("UsdImportContext's 'world' member must point to a valid unreal.World object!")
        return

    context.root_layer_path = exporting_utils.sanitize_filepath(context.root_layer_path)

    unreal.log(f"Starting import from root layer: '{context.root_layer_path}'")

    with unreal.ScopedSlowTask(2, f"Importing level from '{context.root_layer_path}'") as slow_task:
        slow_task.make_dialog(True)

    # Collect prims to import:
    context.stage = Usd.Stage.Open(context.root_layer_path)



def import_with_cdo_options():
    options_cdo = unreal.get_default_object(unreal.USDExtraExportOptions)
    context = usd_extra_export_scripts.UsdExtraExportContext
    context.root_layer_path = options_cdo.file_name
    context.world = options_cdo.world
    context.options = options_cdo

    import_context(context)