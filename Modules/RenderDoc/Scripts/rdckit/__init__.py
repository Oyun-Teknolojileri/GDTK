"""RenderDoc capture analysis kit.

Turns a ``.rdc`` capture into a JSON model, a markdown digest and a folder of
PNG images, so the result can be handed to a human or to an AI for analysis
without opening the RenderDoc UI.

``rdckit.capture``, ``rdckit.images`` and ``rdckit.report`` import the
``renderdoc`` module and therefore only work inside RenderDoc's embedded python
interpreter (see ``rdc_analyze.py``). ``rdckit.util`` and ``rdckit.driver`` are
plain python.
"""

__version__ = "1.0.0"
