// Copyright 2018 Luca Di Sera
//		Contact: disera.luca@gmail.com
//				 https://github.com/diseraluca
//				 www.linkedin.com/in/luca-di-sera-200023167
//
// This code is licensed under the MIT License. 
// More informations can be found in the LICENSE file in the root folder of this repository
//
//
// File : pluginMain.cpp

#include "SingleBlendMeshDeformer.h"

#include <maya/MFnPlugin.h>

MStatus initializePlugin(MObject obj) {
	MStatus status{};
	MFnPlugin plugin{ obj, "Luca Di Sera", "1.0", "Any", &status };
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = plugin.registerNode(SingleBlendMeshDeformer::typeName, SingleBlendMeshDeformer::typeId, SingleBlendMeshDeformer::creator, SingleBlendMeshDeformer::initialize, MPxNode::kDeformerNode);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return MStatus::kSuccess;
}

MStatus uninitializePlugin(MObject obj) {
	MStatus status{};
	MFnPlugin plugin{ obj };

	status = plugin.deregisterNode(SingleBlendMeshDeformer::typeId);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return MStatus::kSuccess;
}