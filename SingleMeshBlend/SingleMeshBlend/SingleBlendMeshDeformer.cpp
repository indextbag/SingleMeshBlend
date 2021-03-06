// Copyright 2018 Luca Di Sera
//		Contact: disera.luca@gmail.com
//				 https://github.com/diseraluca
//				 https://www.linkedin.com/in/luca-di-sera-200023167
//
// This code is licensed under the MIT License. 
// More informations can be found in the LICENSE file in the root folder of this repository
//
//
// File : SingleBlendMeshDeformer.cpp

#include "SingleBlendMeshDeformer.h"

#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MItGeometry.h>
#include <maya/MEvaluationNode.h>

#include <immintrin.h>

MString SingleBlendMeshDeformer::typeName{ "SingleBlendMesh" };
MTypeId SingleBlendMeshDeformer::typeId{ 0x0d12309 };

MObject SingleBlendMeshDeformer::blendMesh;
MObject SingleBlendMeshDeformer::blendWeight;
MObject SingleBlendMeshDeformer::rebind;
MObject SingleBlendMeshDeformer::vertsPerTask;

SingleBlendMeshDeformer::SingleBlendMeshDeformer()
	:isInitialized{ false },
	isThreadDataInitialized{ false },
	lastTaskValue{ 0 },
	taskData{},
	threadData{}
{
	MThreadPool::init();
}

SingleBlendMeshDeformer::~SingleBlendMeshDeformer()
{
	MThreadPool::release();
}

void * SingleBlendMeshDeformer::creator()
{
	return new SingleBlendMeshDeformer();
}

MStatus SingleBlendMeshDeformer::initialize()
{
	MStatus status{};

	MFnTypedAttribute   tAttr{};
	MFnNumericAttribute nAttr{};

	blendMesh = tAttr.create("blendMesh", "blm", MFnData::kMesh, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS(addAttribute(blendMesh));

	blendWeight = nAttr.create("blendWeight", "blw", MFnNumericData::kDouble, 0.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS(nAttr.setKeyable(true));
	CHECK_MSTATUS(nAttr.setMin(0.0));
	CHECK_MSTATUS(nAttr.setMax(1.0));
	CHECK_MSTATUS(addAttribute(blendWeight));

	rebind = nAttr.create("rebind", "rbd", MFnNumericData::kBoolean, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS(nAttr.setKeyable(true));
	CHECK_MSTATUS(addAttribute(rebind));

	// By testing with different meshes 10000 seemed to give the best all-around result
	vertsPerTask = nAttr.create("vertsPerTask", "vpt", MFnNumericData::kInt, 10000, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS(nAttr.setChannelBox(true));
	CHECK_MSTATUS(nAttr.setMin(1));
	CHECK_MSTATUS(addAttribute(vertsPerTask));

	CHECK_MSTATUS(attributeAffects(blendMesh, outputGeom));
	CHECK_MSTATUS(attributeAffects(blendWeight, outputGeom));
	CHECK_MSTATUS(attributeAffects(rebind, outputGeom));

	MGlobal::executeCommand("makePaintable -attrType multiFloat -sm deformer SingleBlendMesh weights");

	return MStatus::kSuccess;
}


MStatus SingleBlendMeshDeformer::deform(MDataBlock & block, MItGeometry & iterator, const MMatrix & matrix, unsigned int multiIndex)
{
	CHECK_MSTATUS_AND_RETURN_IT(iterator.allPositions(taskData.vertexPositions));

	bool rebindValue{ block.inputValue(rebind).asBool() };

	if (!isInitialized | rebindValue) {
		// If blendMesh is not connected we get out
		MPlug blendMeshPlug{ thisMObject(), blendMesh };
		if (!blendMeshPlug.isConnected()) {
			MGlobal::displayWarning(this->name() + ": blendMesh not connected. Please connect a mesh.");
			return MStatus::kInvalidParameter;
		}

		MObject blendMeshValue{ block.inputValue(blendMesh).asMesh() };
		MFnMesh blendMeshFn{ blendMeshValue };

		CHECK_MSTATUS_AND_RETURN_IT( cacheBlendMeshVertexPositionsAndDeltas(blendMeshFn, taskData.vertexPositions) );
		isInitialized = true;
		isThreadDataInitialized = false;
	}

	float envelopeValue{ block.inputValue(envelope).asFloat() };
	double blendWeightValue{ block.inputValue(blendWeight).asDouble() };

	// Setting the relevant attribute values on taskData so that the threads can access them
	taskData.envelopeValue = envelopeValue;
	taskData.blendWeightValue = blendWeightValue;

	// Initialize thead data
	int vertsPerTaskValue{ block.inputValue(vertsPerTask).asInt() };
	if (!isThreadDataInitialized | ( lastTaskValue != vertsPerTaskValue )) {
		createThreadData(vertsPerTaskValue, &taskData);
		isThreadDataInitialized = true;
		lastTaskValue = vertsPerTaskValue;
	}

	MThreadPool::newParallelRegion(createTasks, (void*)&threadData);

	iterator.setAllPositions(taskData.vertexPositions);

	return MStatus::kSuccess;
}

void SingleBlendMeshDeformer::createThreadData(int vertsPerTask, TaskData * taskData)
{
	// Calculate the number of task needed and allocate the required memory
	unsigned int vertexCount{ taskData->vertexPositions.length() };
	unsigned int numTasks = (vertexCount < vertsPerTask) ? 1 : ( vertexCount / vertsPerTask );
	this->threadData.clear();
	this->threadData.resize(numTasks);
	
	unsigned int start{ 0 };
	unsigned int end{ (unsigned int)vertsPerTask };

	for (unsigned int taskIndex{ 0 }; taskIndex < numTasks; ++taskIndex) {

		threadData[taskIndex].start = start;
		threadData[taskIndex].end = end;
		threadData[taskIndex].numTasks = numTasks;
		threadData[taskIndex].data = taskData;

		start += vertsPerTask;
		end += vertsPerTask;
	}

	// We patch the last task to end at the last vertex
	threadData[numTasks - 1].end = vertexCount;
}

void SingleBlendMeshDeformer::createTasks(void * data, MThreadRootTask * pRoot)
{
	std::vector<ThreadData>* threadData{ static_cast<std::vector<ThreadData>*>(data) };

	if (threadData) {
		unsigned int numTasks{ (*threadData)[0].numTasks };
		for (unsigned int taskIndex{ 0 }; taskIndex < numTasks; taskIndex++) {
			MThreadPool::createTask(threadEvaluate, (void*)(&(*threadData)[taskIndex]), pRoot);
		}
		MThreadPool::executeAndJoin(pRoot);
	}
}

MThreadRetVal SingleBlendMeshDeformer::threadEvaluate(void * pParam)
{
	MStatus status{};

	ThreadData* threadData{ (ThreadData*)pParam };
	TaskData* data{ threadData->data };

	unsigned int start{ threadData->start };
	unsigned int end{ threadData->end };

	// Maya containers present some overhead for their operations.
	// Accessing the memory directly bypasses that overhead
	double* currentVertexPosition{ &data->vertexPositions[start].x };
	const double* currentDeltaVector{ &data->deltas[start].x };

	float envelopeValue{ data->envelopeValue };
	double blendWeightValue{ data->blendWeightValue };
	double deltaWeight{ blendWeightValue * envelopeValue };

	__m256d deltaWeightVector = _mm256_set1_pd(deltaWeight);

	// The pointers are updated to the next stored value by jumping X doubles ahead depending on the container they are pointing to.
	// 4 doubles are for MPointArrays and 3 doubles are for MVectorArrays
	for (unsigned int vertexIndex{ start }; vertexIndex < end; ++vertexIndex, currentVertexPosition += 4, currentDeltaVector += 3 ) {
		__m256d vertexPosition = _mm256_load_pd(currentVertexPosition);

		// Deltas contains 3 relevant values. 4 doubles are loaded for AVX vectors so the last value must be masked.
		// Positive number ( whose highest bit is one ) mask that data to zero.
		__m256i deltaMask = _mm256_setr_epi32(-1, -1, -1, -1, -1, -1, 1, 1);
		__m256d deltaVector = _mm256_maskload_pd(currentDeltaVector, deltaMask);

		__m256d partialResult = _mm256_mul_pd(deltaVector, deltaWeightVector);
		__m256d resultPosition = _mm256_add_pd(partialResult, vertexPosition);

		_mm256_store_pd(currentVertexPosition, resultPosition);
	}

	return MThreadRetVal();
}

MStatus SingleBlendMeshDeformer::cacheBlendMeshVertexPositionsAndDeltas(const MFnMesh & blendMeshFn, const MPointArray& vertexPositions)
{
	MStatus status{};

	int vertexCount{ blendMeshFn.numVertices(&status) };
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Cache blend vertex Positions
	MPointArray blendVertexPositions{};
	blendVertexPositions.setLength(vertexCount);
	CHECK_MSTATUS_AND_RETURN_IT( blendMeshFn.getPoints(blendVertexPositions) );

	cacheDeltasValues(vertexPositions, blendVertexPositions, vertexCount);

	return MStatus::kSuccess;
}

MStatus SingleBlendMeshDeformer::cacheDeltasValues(const MPointArray & vertexPositions, const MPointArray & blendVertexPositions, int vertexCount)
{
	taskData.deltas.setLength(vertexCount);
	for (int vertexIndex{ 0 }; vertexIndex < vertexCount; ++vertexIndex) {
		taskData.deltas[vertexIndex] = blendVertexPositions[vertexIndex] - vertexPositions[vertexIndex];
	}

	return MStatus::kSuccess;
}
