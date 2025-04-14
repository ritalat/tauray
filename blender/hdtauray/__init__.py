bl_info = {
    "name": "Tauray Render Engine",
    "category": "Render",
    "blender": (4, 0, 0),
    "version": (0, 1),
    'description': 'Tauray Hydra render delegate.',
}

# FIXME: Expose more render settings
# FIXME: Expose depth buffer
# FIXME: Separate viewport settings?

import bpy

class TaurayHydraRenderEngine(bpy.types.HydraRenderEngine):
    bl_idname = "TAURAY_HYDRA_RENDERER"
    bl_label = "Tauray (Hydra)"

    bl_delegate_id = "tr::HdTaurayRendererPlugin"

    bl_use_materialx = False

    @classmethod
    def register(cls):
        bpy.utils.expose_bundled_modules()

        import os
        import pxr.Plug
        # FIXME: :D
        pxr.Plug.Registry().RegisterPlugins([os.getenv("PXR_PLUGINPATH_NAME")])

    def get_render_settings(self, engine_type):
        return {
            'accumulate': True,
            'samples': 128,
            'sampleBatch': 1,
            'rayDepth': 8,
            'aovToken:Combined': "color",
        }

def register():
    bpy.utils.register_class(TaurayHydraRenderEngine)


def unregister():
    bpy.utils.unregister_class(TaurayHydraRenderEngine)


if __name__ == "__main__":
    register()
