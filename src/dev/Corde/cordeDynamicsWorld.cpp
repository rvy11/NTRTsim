/*
 * Copyright © 2012, United States Government, as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All rights reserved.
 * 
 * The NASA Tensegrity Robotics Toolkit (NTRT) v1 platform is licensed
 * under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0.
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language
 * governing permissions and limitations under the License.

This class is a modified version of btSoftRigidDynamicsWorld
* from Bullet 2.82. A copy of the z-lib license from the Bullet Physics
* Library is provided below:

Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2003-2006 Erwin Coumans  http://continuousphysics.com/Bullet/

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/


#include "cordeDynamicsWorld.h"
#include "cordeCollisionObject.h"
#include "cordeSolvers.h"
#include "defaultCordeSolver.h"
#include "LinearMath/btQuickprof.h"

//softbody & helpers
#include "LinearMath/btSerializer.h"

cordeDynamicsWorld::cordeDynamicsWorld(
	btDispatcher* dispatcher,
	btBroadphaseInterface* pairCache,
	btConstraintSolver* constraintSolver,
	btCollisionConfiguration* collisionConfiguration,
	cordeSolver *softBodySolver ) : 
		btDiscreteDynamicsWorld(dispatcher,pairCache,constraintSolver,collisionConfiguration),
		m_softBodySolver( softBodySolver ),
		m_ownsSolver(false)
{
	if( !m_softBodySolver )
	{
		void* ptr = btAlignedAlloc(sizeof(defaultCordeSolver),16);
		m_softBodySolver = new(ptr) defaultCordeSolver();
		m_ownsSolver = true;
	}

}

cordeDynamicsWorld::~cordeDynamicsWorld()
{
	if (m_ownsSolver)
	{
		m_softBodySolver->~cordeSolver();
		btAlignedFree(m_softBodySolver);
	}
}

void	cordeDynamicsWorld::predictUnconstraintMotion(btScalar timeStep)
{
	btDiscreteDynamicsWorld::predictUnconstraintMotion( timeStep );
	{
		BT_PROFILE("predictUnconstraintMotionSoftBody");
		m_softBodySolver->predictMotion( timeStep );
	}
}

void	cordeDynamicsWorld::internalSingleStepSimulation( btScalar timeStep )
{

	// Let the solver grab the soft bodies and if necessary optimize for it
	m_softBodySolver->optimize( getSoftBodyArray() );

	if( !m_softBodySolver->checkInitialized() )
	{
		btAssert( "Solver initialization failed\n" );
	}
	
	// Includes predictUnconstraintMotion
	btDiscreteDynamicsWorld::internalSingleStepSimulation( timeStep );

	///solve soft bodies constraints (anchors and such)
	solveSoftBodiesConstraints( timeStep );

	//self collisions
	for ( int i=0;i<m_cordeObjects.size();i++)
	{
		cordeCollisionObject*	psb=(cordeCollisionObject*)m_cordeObjects[i];
		psb->defaultCollisionHandler(psb);
	}
	
	///update soft bodies
	m_softBodySolver->updateSoftBodies(timeStep );
	
	// End solver-wise simulation step
	// ///////////////////////////////

}

void	cordeDynamicsWorld::solveSoftBodiesConstraints( btScalar timeStep )
{
	BT_PROFILE("solveSoftConstraints");

	// Solve constraints solver-wise
	m_softBodySolver->solveConstraints( timeStep * m_softBodySolver->getTimeScale() );

}

void	cordeDynamicsWorld::addSoftBody(cordeCollisionObject* body,short int collisionFilterGroup,short int collisionFilterMask)
{
	m_cordeObjects.push_back(body);

	// Set the soft body solver that will deal with this body
	// to be the world's solver
	body->setSolver( m_softBodySolver );

	btCollisionWorld::addCollisionObject(body,
		collisionFilterGroup,
		collisionFilterMask);

}

void	cordeDynamicsWorld::removeSoftBody(cordeCollisionObject* body)
{
	m_cordeObjects.remove(body);

	btCollisionWorld::removeCollisionObject(body);
}

void	cordeDynamicsWorld::removeCollisionObject(btCollisionObject* collisionObject)
{
	cordeCollisionObject* body = cordeCollisionObject::upcast(collisionObject);
	if (body)
		removeSoftBody(body);
	else
		btDiscreteDynamicsWorld::removeCollisionObject(collisionObject);
}

void	cordeDynamicsWorld::debugDrawWorld()
{
	btDiscreteDynamicsWorld::debugDrawWorld();
		
	// tgBulletRenderer handles this step for us
}


#if (0) // Temporarily disable raytesting

struct btSoftSingleRayCallback : public btBroadphaseRayCallback
{
	btVector3	m_rayFromWorld;
	btVector3	m_rayToWorld;
	btTransform	m_rayFromTrans;
	btTransform	m_rayToTrans;
	btVector3	m_hitNormal;

	const cordeDynamicsWorld*	m_world;
	btCollisionWorld::RayResultCallback&	m_resultCallback;

	btSoftSingleRayCallback(const btVector3& rayFromWorld,const btVector3& rayToWorld,const cordeDynamicsWorld* world,btCollisionWorld::RayResultCallback& resultCallback)
	:m_rayFromWorld(rayFromWorld),
	m_rayToWorld(rayToWorld),
	m_world(world),
	m_resultCallback(resultCallback)
	{
		m_rayFromTrans.setIdentity();
		m_rayFromTrans.setOrigin(m_rayFromWorld);
		m_rayToTrans.setIdentity();
		m_rayToTrans.setOrigin(m_rayToWorld);

		btVector3 rayDir = (rayToWorld-rayFromWorld);

		rayDir.normalize ();
		///what about division by zero? --> just set rayDirection[i] to INF/1e30
		m_rayDirectionInverse[0] = rayDir[0] == btScalar(0.0) ? btScalar(1e30) : btScalar(1.0) / rayDir[0];
		m_rayDirectionInverse[1] = rayDir[1] == btScalar(0.0) ? btScalar(1e30) : btScalar(1.0) / rayDir[1];
		m_rayDirectionInverse[2] = rayDir[2] == btScalar(0.0) ? btScalar(1e30) : btScalar(1.0) / rayDir[2];
		m_signs[0] = m_rayDirectionInverse[0] < 0.0;
		m_signs[1] = m_rayDirectionInverse[1] < 0.0;
		m_signs[2] = m_rayDirectionInverse[2] < 0.0;

		m_lambda_max = rayDir.dot(m_rayToWorld-m_rayFromWorld);

	}

	

	virtual bool	process(const btBroadphaseProxy* proxy)
	{
		///terminate further ray tests, once the closestHitFraction reached zero
		if (m_resultCallback.m_closestHitFraction == btScalar(0.f))
			return false;

		btCollisionObject*	collisionObject = (btCollisionObject*)proxy->m_clientObject;

		//only perform raycast if filterMask matches
		if(m_resultCallback.needsCollision(collisionObject->getBroadphaseHandle())) 
		{
			//RigidcollisionObject* collisionObject = ctrl->GetRigidcollisionObject();
			//btVector3 collisionObjectAabbMin,collisionObjectAabbMax;
#if 0
#ifdef RECALCULATE_AABB
			btVector3 collisionObjectAabbMin,collisionObjectAabbMax;
			collisionObject->getCollisionShape()->getAabb(collisionObject->getWorldTransform(),collisionObjectAabbMin,collisionObjectAabbMax);
#else
			//getBroadphase()->getAabb(collisionObject->getBroadphaseHandle(),collisionObjectAabbMin,collisionObjectAabbMax);
			const btVector3& collisionObjectAabbMin = collisionObject->getBroadphaseHandle()->m_aabbMin;
			const btVector3& collisionObjectAabbMax = collisionObject->getBroadphaseHandle()->m_aabbMax;
#endif
#endif
			//btScalar hitLambda = m_resultCallback.m_closestHitFraction;
			//culling already done by broadphase
			//if (btRayAabb(m_rayFromWorld,m_rayToWorld,collisionObjectAabbMin,collisionObjectAabbMax,hitLambda,m_hitNormal))
			{
				m_world->rayTestSingle(m_rayFromTrans,m_rayToTrans,
					collisionObject,
						collisionObject->getCollisionShape(),
						collisionObject->getWorldTransform(),
						m_resultCallback);
			}
		}
		return true;
	}
};

void	cordeDynamicsWorld::rayTest(const btVector3& rayFromWorld, const btVector3& rayToWorld, RayResultCallback& resultCallback) const
{
	BT_PROFILE("rayTest");
	/// use the broadphase to accelerate the search for objects, based on their aabb
	/// and for each object with ray-aabb overlap, perform an exact ray test
	btSoftSingleRayCallback rayCB(rayFromWorld,rayToWorld,this,resultCallback);

#ifndef USE_BRUTEFORCE_RAYBROADPHASE
	m_broadphasePairCache->rayTest(rayFromWorld,rayToWorld,rayCB);
#else
	for (int i=0;i<this->getNumCollisionObjects();i++)
	{
		rayCB.process(m_collisionObjects[i]->getBroadphaseHandle());
	}	
#endif //USE_BRUTEFORCE_RAYBROADPHASE

}


void	cordeDynamicsWorld::rayTestSingle(const btTransform& rayFromTrans,const btTransform& rayToTrans,
					  btCollisionObject* collisionObject,
					  const btCollisionShape* collisionShape,
					  const btTransform& colObjWorldTransform,
					  RayResultCallback& resultCallback)
{
	if (collisionShape->isSoftBody()) {
		cordeCollisionObject* softBody = btSoftBody::upcast(collisionObject);
		if (softBody) {
			btSoftBody::sRayCast softResult;
			if (softBody->rayTest(rayFromTrans.getOrigin(), rayToTrans.getOrigin(), softResult)) 
			{
				
				if (softResult.fraction<= resultCallback.m_closestHitFraction)
				{

					btCollisionWorld::LocalShapeInfo shapeInfo;
					shapeInfo.m_shapePart = 0;
					shapeInfo.m_triangleIndex = softResult.index;
					// get the normal
					btVector3 rayDir = rayToTrans.getOrigin() - rayFromTrans.getOrigin();
					btVector3 normal=-rayDir;
					normal.normalize();

					if (softResult.feature == btSoftBody::eFeature::Face)
					{
						normal = softBody->m_faces[softResult.index].m_normal;
						if (normal.dot(rayDir) > 0) {
							// normal always point toward origin of the ray
							normal = -normal;
						}
					}
	
					btCollisionWorld::LocalRayResult rayResult
						(collisionObject,
						 &shapeInfo,
						 normal,
						 softResult.fraction);
					bool	normalInWorldSpace = true;
					resultCallback.addSingleResult(rayResult,normalInWorldSpace);
				}
			}
		}
	} 
	else {
		btCollisionWorld::rayTestSingle(rayFromTrans,rayToTrans,collisionObject,collisionShape,colObjWorldTransform,resultCallback);
	}
}
#endif // Disable raytest temp

#if (0)
void	cordeDynamicsWorld::serializeSoftBodies(btSerializer* serializer)
{
	int i;
	//serialize all collision objects
	for (i=0;i<m_collisionObjects.size();i++)
	{
		btCollisionObject* colObj = m_collisionObjects[i];
		if (colObj->getInternalType() & btCollisionObject::CO_SOFT_BODY)
		{
			int len = colObj->calculateSerializeBufferSize();
			btChunk* chunk = serializer->allocate(len,1);
			const char* structType = colObj->serialize(chunk->m_oldPtr, serializer);
			serializer->finalizeChunk(chunk,structType,BT_SOFTBODY_CODE,colObj);
		}
	}

}
#endif // Disable serialize

void	cordeDynamicsWorld::serialize(btSerializer* serializer)
{

	serializer->startSerialization();

	serializeDynamicsWorldInfo( serializer);
#if (0)
	serializeSoftBodies(serializer); ///@todo
#endif
	serializeRigidBodies(serializer);

	serializeCollisionObjects(serializer);

	serializer->finishSerialization();
}


