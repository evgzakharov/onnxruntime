# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
# checkpoint_utils.py

from onnxruntime.capi._pybind_state import load_checkpoint as _internal_load_checkpoint
from onnxruntime.capi._pybind_state import save_checkpoint as _internal_save_checkpoint


def save_checkpoint(parameters, path_to_checkpoint):
    """Saves the parameters to the checkpoint directory path_to_checkpoint."""

    if parameters is None:
        raise RuntimeError("No checkpoint parameters provided.")

    # TODO: use Parameter class to pass information to backend
    # Serialize the parameters and save the checkpoint
    trainable_params, non_trainable_params = parameters
    trainable_params = [param.SerializeToString() for param in trainable_params]
    non_trainable_params = [param.SerializeToString() for param in non_trainable_params]
    _internal_save_checkpoint(trainable_params, non_trainable_params, path_to_checkpoint)


def load_checkpoint(model, path_to_checkpoint):
    """Loads the checkpoint to an onnx inference model."""

    # Load the parameters from the checkpoint
    parameters = _internal_load_checkpoint(path_to_checkpoint)