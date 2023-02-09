/*
 * CompoundSolverDefault.cpp
 *
 *  Created on: Mar 23, 2022
 *      Author: mballance
 */

#include "dmgr/impl/DebugMacros.h"
#include "vsc/dm/impl/PrettyPrinter.h"
#include "vsc/dm/impl/TaskUnrollModelIterativeConstraints.h"
#include "vsc/dm/impl/TaskRollbackConstraintSubst.h"
#include "CommitFieldValueVisitor.h"
#include "CompoundSolverDefault.h"
#include "SolverFactoryDefault.h"
#include "SolveSetSolveModelBuilder.h"
#include "SolveSetSwizzlerPartsel.h"
#include "SolveSpecBuilder.h"
#include "vsc/dm/impl/TaskSetUsedRand.h"
#include "TaskResizeConstrainedModelVec.h"

namespace vsc {
namespace solvers {


CompoundSolverDefault::CompoundSolverDefault(
    dm::IContext        *ctxt,
    ISolverFactory      *solver_factory) : m_ctxt(ctxt), m_solver_factory(solver_factory) {
	DEBUG_INIT("CompountSolverDefault", ctxt->getDebugMgr());

}

CompoundSolverDefault::~CompoundSolverDefault() {
	// TODO Auto-generated destructor stub
}

bool CompoundSolverDefault::solve(
			IRandState								    *randstate,
			const std::vector<dm::IModelField *>		&fields,
			const std::vector<dm::IModelConstraint *>	&constraints,
			SolveFlags								flags) {
	bool ret = true;
	DEBUG_ENTER("randomize n_fields=%d n_constraints=%d",
			fields.size(),
			constraints.size());

	if ((flags & SolveFlags::RandomizeDeclRand) || (flags & SolveFlags::RandomizeTopFields)) {
		dm::TaskSetUsedRand task;
		for (auto f=fields.begin(); f!=fields.end(); f++) {
			task.apply(*f,
					(flags & SolveFlags::RandomizeTopFields) != SolveFlags::NoFlags,
					((flags & SolveFlags::RandomizeDeclRand) != SolveFlags::NoFlags)?-1:1);
			DEBUG("Flags: %s 0x%08llx", (*f)->name().c_str(), (*f)->flags());
		}
	}

	SolveSpecUP spec(SolveSpecBuilder(m_ctxt).build(
			fields,
			constraints
			));

	DEBUG("%d solve-sets ; %d unconstrained ; %d unconstrained_sz_vec",
			spec->solvesets().size(),
			spec->unconstrained().size(),
			spec->unconstrained_sz_vec().size());

	// Start by fixing the size of the unconstrained-size vectors
	// to the current size
	for (std::vector<dm::IModelFieldVec *>::const_iterator
		it=spec->unconstrained_sz_vec().begin();
		it!=spec->unconstrained_sz_vec().end(); it++) {
		(*it)->getSizeRef()->val()->set_val_u((*it)->getSize(), 32);
		(*it)->getSizeRef()->setFlag(dm::ModelFieldFlag::Resolved);
	}

	for (auto uc_it=spec->unconstrained().begin();
			uc_it!=spec->unconstrained().end(); uc_it++) {
		DEBUG("Randomize unconstrained field %s", (*uc_it)->name().c_str());
		randstate->randbits((*uc_it)->val());
	}

	for (auto sset=spec->solvesets().begin();
			sset!=spec->solvesets().end(); sset++) {
		DEBUG("Solve Set: %d fields ; %d constraints",
				(*sset)->all_fields().size(),
				(*sset)->constraints().size());

		// See if we need to re-evaluate due to vector constraints
		// TODO: Apply vector-sizing task
		if ((*sset)->constrained_sz_vec().size() > 0) {
			fprintf(stdout, "TODO: apply sizing to vectors\n");

			TaskResizeConstrainedModelVec(m_ctxt, m_solver_factory).resize(sset->get());
		} 

		if ((*sset)->hasFlags(SolveSetFlag::HaveForeach)) {
			// This solve-set has foreach constraints. Need to expand
			// the constraints, etc
			fprintf(stdout, "TODO: expand foreach constraints\n");
			dm::TaskUnrollModelIterativeConstraints unroller(m_ctxt);
			dm::IModelConstraintScopeUP unroll_s(m_ctxt->mkModelConstraintScope());
			for (std::vector<dm::IModelConstraint *>::const_iterator
				it=(*sset)->constraints().begin();
				it!=(*sset)->constraints().end(); it++) {
				if (dynamic_cast<dm::IModelConstraintScope *>(*it)) {
					unroller.unroll(unroll_s.get(), *it);
				}
			}

			DEBUG("unroll_s: %d constraints", unroll_s->getConstraints().size());

			// Now, partition up the new expanded set
			SolveSpecUP spec_it(SolveSpecBuilder(m_ctxt).build(
				{}, {unroll_s.get()}));

			DEBUG("spec_it: %d solve-sets ; %d unconstrained ; %d unconstrained_sz_vec",
					spec_it->solvesets().size(),
					spec_it->unconstrained().size(),
					spec_it->unconstrained_sz_vec().size());

			for (std::vector<SolveSetUP>::const_iterator 
					sset_it=spec_it->solvesets().begin();
					sset_it!=spec_it->solvesets().end(); sset_it++) {
				DEBUG("Solve Set It: %d fields ; %d constraints",
						(*sset_it)->all_fields().size(),
						(*sset_it)->constraints().size());
				solve_sset(sset_it->get(), m_solver_factory, randstate, flags);
			}

			// TODO: roll-back the variables in the solve-set to 
			// reverse the  result of unrolling
			for (std::vector<dm::IModelConstraint *>::const_iterator
				it=(*sset)->constraints().begin();
				it!=(*sset)->constraints().end(); it++) {
				dm::TaskRollbackConstraintSubst().rollback(*it);
			}

		} else {
			// If vector-sizing has no effect on this solve set, then proceed
			// to solve and randomize the existing solveset
			if (!(ret=solve_sset(sset->get(), m_solver_factory, randstate, flags))) {
				break;
			}
		}
	}


	DEBUG_LEAVE("randomize n_fields=%d n_constraints=%d ret=%d",
			fields.size(),
			constraints.size(),
			ret);
	return ret;
}

bool CompoundSolverDefault::solve_sset(
	SolveSet			*sset,
	ISolverFactory		*solver_f,
	IRandState			*randstate,
	SolveFlags			flags) {
	DEBUG_ENTER("solve_sset");
	bool ret = true;

	ISolverUP solver(solver_f->createSolverInst(m_ctxt, sset));
	// Build solve data for this solve set
	SolveSetSolveModelBuilder(solver.get()).build(sset);

/*
	for (std::vector<IModelConstraint *>::const_iterator
		it=sset->constraints().begin();
		it!=sset->constraints().end(); it++) {
		DEBUG("Constraint: %s", PrettyPrinter().print(*it));
	}
 */

	// First, ensure all constraints solve
	for (auto c_it=sset->constraints().begin();
			c_it!=sset->constraints().end(); c_it++) {
		solver->addAssume(*c_it);
	}
	for (auto c_it=sset->soft_constraints().begin();
			c_it!=sset->soft_constraints().end(); c_it++) {
		solver->addAssume(*c_it);
	}

	if (solver->isSAT()) {
		DEBUG("PASS: Initial try-solve for solveset");
		for (auto c_it=sset->constraints().begin();
			c_it!=sset->constraints().end(); c_it++) {
			solver->addAssert(*c_it);
		}
	} else {
		DEBUG("FAIL: Initial try-solve for solveset");

		ret = false;

		// TODO: Try backing off soft constraints
	}

	if (ret) {
		if ((flags & SolveFlags::Randomize) != SolveFlags::NoFlags) {
			// Swizzle fields
			SolveSetSwizzlerPartsel(m_ctxt, randstate).swizzle(
					solver.get(),
					sset);
		}

		// Ensure we're SAT
		if (!solver->isSAT()) {
			fprintf(stdout, "unsat post-swizzle\n");
		}

		for (auto f_it=sset->rand_fields().begin();
				f_it!=sset->rand_fields().end(); f_it++) {
			DEBUG("Commit %s", (*f_it)->name().c_str());
			CommitFieldValueVisitor(solver.get()).commit(*f_it);
		}
	}

	DEBUG_LEAVE("solve_sset");
	return ret;
}

dmgr::IDebug *CompoundSolverDefault::m_dbg = 0;

}
}

