                

1. we look at how the contactSolver solves collisions

    notice that both contactSolver.SolvePositionConstraints(); and m_joints[j]->SolvePositionConstraints(solverData);
    return a boolean. The idea is that, inside both SolvePositionConstraints and SolvePositionConstraints,
    we will return true if the the error in our position constraints are under a certain limit. 

    this way we only need to run a certain number of iterations as needed. 
    as you can see, we early out when contactsOkay is true, which we mark positionSolved to be true.

                b2Island.cpp

                void b2Island::Solve(b2Profile* profile, const b2TimeStep& step, const b2Vec2& gravity, bool allowSleep)
                {

                    ...
                    ...

                    // Solve position constraints
                    timer.Reset();
                    bool positionSolved = false;
                    for (int32 i = 0; i < step.positionIterations; ++i)
                    {
                        bool contactsOkay = contactSolver.SolvePositionConstraints();

                        bool jointsOkay = true;
                        for (int32 j = 0; j < m_jointCount; ++j)
                        {
                            bool jointOkay = m_joints[j]->SolvePositionConstraints(solverData);
                            jointsOkay = jointsOkay && jointOkay;
                        }

                        if (contactsOkay && jointsOkay)
                        {
                            // Exit early if the position errors are small.
                            positionSolved = true;
                            break;
                        }
                    }

                    ...
                    ...
                }


    the positionSolved flag will be used in the "sleeping" logic, which we will address in the next few chapter. 


3.  we create the b2PositionSolverManifold, which is really just collision contact data

                bool b2ContactSolver::SolvePositionConstraints()
                {
                    float32 minSeparation = 0.0f;

                    for (int32 i = 0; i < m_count; ++i)
                    {
                        b2ContactPositionConstraint* pc = m_positionConstraints + i;

                        ...
                        ...

                        // Solve normal constraints
                        for (int32 j = 0; j < pointCount; ++j)
                        {

                            b2Transform xfA, xfB;
                            xfA.q.Set(aA);
                            xfB.q.Set(aB);
                            xfA.p = cA - b2Mul(xfA.q, localCenterA);
                            xfB.p = cB - b2Mul(xfB.q, localCenterB);

                            b2PositionSolverManifold psm;
                            psm.Initialize(pc, xfA, xfB, j);
                            b2Vec2 normal = psm.normal;

                            b2Vec2 point = psm.point;
                            float32 separation = psm.separation;

                            ...
                            ...
                        }
                    }
                }




4.  let us look at what the b2PositionSolverManifold is.
    
    the b2PositionSolverManifold contains just three variables:

                b2Vec2 normal;
                b2Vec2 point;
                float32 separation;


    and here you can see we are calling the Initialize(); method

                b2PositionSolverManifold

                struct b2PositionSolverManifold
                {
                    void Initialize(b2ContactPositionConstraint* pc, const b2Transform& xfA, const b2Transform& xfB, int32 index)
                    {
                        b2Assert(pc->pointCount > 0);

                        switch (pc->type)
                        {
                        case b2Manifold::e_circles:
                            {
                                b2Vec2 pointA = b2Mul(xfA, pc->localPoint);
                                b2Vec2 pointB = b2Mul(xfB, pc->localPoints[0]);
                                normal = pointB - pointA;
                                normal.Normalize();
                                point = 0.5f * (pointA + pointB);
                                separation = b2Dot(pointB - pointA, normal) - pc->radiusA - pc->radiusB;
                            }
                            break;

                        case b2Manifold::e_faceA:
                            {
                                ...
                                ...
                            }
                            break;

                        case b2Manifold::e_faceB:
                            {
                                ...
                                ...
                            }
                            break;
                        }
                    }

                    ...
                    ...
                };

here we are only listing the circle vs circle. Notice that separation is negative. 

if the two circles arent colliding 

                b2Dot(pointB - pointA, normal)  > (pc->radiusA + pc->radiusB);

so here separation is negative 



5.  once we are done preparing, we calculate our position correction.
    For the main algorithm, we are just doing the Baumgarte Stabilization 


    the formula we are using for the positional correction (before clamping); is  

                b2_baumgarte * (separation + b2_linearSlop);


    b2_linearSlop means we allow objects to penetrate a bit.





-   the b2_baumgarte is also defined in b2Settings.h 

                /// This scale factor controls how fast overlap is resolved. Ideally this would be 1 so
                /// that overlap is removed in one time step. However using values close to 1 often lead
                /// to overshoot.
                #define b2_baumgarte                0.2f

    you can see the comments 



-   the b2_maxLinearCorrection variable is defined in b2Settings.h 

                /// The maximum linear position correction used when solving constraints. This helps to
                /// prevent overshoot.
                #define b2_maxLinearCorrection      0.2f



-   we track the max constraint error

                // Track max constraint error.
                minSeparation = b2Min(minSeparation, separation);

                ...
                ...

                minSeparation >= -3.0f * b2_linearSlop;

    notice that SolvePositionConstraints(); will return true or false. and we return true 
    only if we are within reasonable limits; this is where we return true or false.

            

-   as you can see, we compute the correction we want to use, 
    then apply it on to the positions and orientations

                cA -= mA * P;
                aA -= iA * b2Cross(rA, P);

                cB += mB * P;
                aB += iB * b2Cross(rB, P);


-   full code below:

                b2ContactSolver.cpp

                bool b2ContactSolver::SolvePositionConstraints()
                {
                    float32 minSeparation = 0.0f;

                    for (int32 i = 0; i < m_count; ++i)
                    {
                        b2ContactPositionConstraint* pc = m_positionConstraints + i;

                        ...
                        ...

                        // Solve normal constraints
                        for (int32 j = 0; j < pointCount; ++j)
                        {
                            ...
                            ...

                            b2Vec2 cA = m_positions[indexA].c;
                            float32 aA = m_positions[indexA].a;

                            b2Vec2 cB = m_positions[indexB].c;
                            float32 aB = m_positions[indexB].a;

                            b2PositionSolverManifold psm;
                            psm.Initialize(pc, xfA, xfB, j);
                            b2Vec2 normal = psm.normal;

                            b2Vec2 point = psm.point;
                            float32 separation = psm.separation;

                            b2Vec2 rA = point - cA;
                            b2Vec2 rB = point - cB;

                            // Track max constraint error.
                            minSeparation = b2Min(minSeparation, separation);

                            // Prevent large corrections and allow slop.
                            float32 correction = b2Clamp(b2_baumgarte * (separation + b2_linearSlop), -b2_maxLinearCorrection, 0.0f);

                            // Compute the effective mass.
                            float32 rnA = b2Cross(rA, normal);
                            float32 rnB = b2Cross(rB, normal);
                            float32 totalEffectiveMass = mA + mB + iA * rnA * rnA + iB * rnB * rnB;

                            // Compute normal impulse
                            float32 impulse = totalEffectiveMass > 0.0f ? - correction / totalEffectiveMass : 0.0f;

                            b2Vec2 P = impulse * normal;

                            cA -= mA * P;
                            aA -= iA * b2Cross(rA, P);

                            cB += mB * P;
                            aB += iB * b2Cross(rB, P);
                        }

                        m_positions[indexA].c = cA;
                        m_positions[indexA].a = aA;

                        m_positions[indexB].c = cB;
                        m_positions[indexB].a = aB;
                    }

                    // We can't expect minSpeparation >= -b2_linearSlop because we don't
                    // push the separation above -b2_linearSlop.
                    return minSeparation >= -3.0f * b2_linearSlop;
                }




