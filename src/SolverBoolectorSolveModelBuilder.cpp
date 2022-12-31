/*
 * Copyright 2019-2021 Matthew Ballance and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SolverBoolectorModelBuilder.cpp
 *
 *  Created on: Sep 26, 2021
 *      Author: mballance
 */

#include "dmgr/impl/DebugMacros.h"
#include "SolverBoolectorSolveModelBuilder.h"
#include "boolector/boolector.h"
#include "vsc/dm/impl/TaskIsDataTypeSigned.h"

namespace vsc {
namespace solvers {


SolverBoolectorSolveModelBuilder::SolverBoolectorSolveModelBuilder(
        dmgr::IDebugMgr     *dmgr,
		SolverBoolector     *solver) : m_solver(solver), m_build_field(false) {
    DEBUG_INIT("SolverBoolectorSolveModelBuilder", dmgr);
}

SolverBoolectorSolveModelBuilder::~SolverBoolectorSolveModelBuilder() {
	// TODO Auto-generated destructor stub
}

BoolectorNode *SolverBoolectorSolveModelBuilder::build(dm::IModelField *f) {
	DEBUG_ENTER("build(Field)");
	m_build_field = true;
	m_width_s.clear();
	m_width_s.push_back(-1);

	f->accept(this);

	DEBUG_LEAVE("build(Field)");
	return m_node_i.second;
}

BoolectorNode *SolverBoolectorSolveModelBuilder::build(dm::IModelConstraint *c) {
	DEBUG_ENTER("build(Constraint)");
	m_build_field = false;
	m_width_s.clear();
	m_width_s.push_back(-1);

	m_node_i = {false, 0};

	c->accept(this);

	if (!m_node_i.second) {
		// Create a '1' node
		DEBUG("CheckMe: returning '1' node");
		return boolector_one(
				m_solver->btor(),
				m_solver->get_sort(1));
	} else {
		return m_node_i.second;
	}

	DEBUG_LEAVE("build(Constraint)");
}

void SolverBoolectorSolveModelBuilder::visitDataTypeEnum(dm::IDataTypeEnum *t) {
	DEBUG_ENTER("visitDataTypeEnum");
	int32_t width = t->getWidth(); // TODO: should calculate from definition
	BoolectorNode *n = boolector_var(m_solver->btor(),
			m_solver->get_sort(width), 0);
	m_node_i = {t->isSigned(), n};
	DEBUG_LEAVE("visitDataTypeEnum");
}

void SolverBoolectorSolveModelBuilder::visitDataTypeInt(dm::IDataTypeInt *t) {
	DEBUG_ENTER("visitDataTypeInt width=%d", t->width());
	BoolectorNode *n = boolector_var(m_solver->btor(),
			m_solver->get_sort(t->width()), 0);
	m_node_i = {t->is_signed(), n};
	DEBUG_LEAVE("visitDataTypeInt");
}

void SolverBoolectorSolveModelBuilder::visitDataTypeStruct(dm::IDataTypeStruct *t) {

}

void SolverBoolectorSolveModelBuilder::visitModelConstraint(dm::IModelConstraint *c) {

}

void SolverBoolectorSolveModelBuilder::visitModelConstraintExpr(dm::IModelConstraintExpr *c) {
	DEBUG_ENTER("visitModelConstraintExpr");
	m_node_i = {false, 0};

	m_width_s.push_back(1);
	c->expr()->accept(this);
	m_width_s.pop_back();

	if (!m_node_i.second) {
		fprintf(stdout, "Error: constraint not built\n");
	}

	// Ensure we have a boolean result
	m_node_i.second = toBoolNode(m_node_i.second);

	DEBUG_LEAVE("visitModelConstraintExpr");
}

void SolverBoolectorSolveModelBuilder::visitModelConstraintIfElse(dm::IModelConstraintIfElse *c) {
	DEBUG_ENTER("visitModelConstrainIfElse");
	c->getCond()->accept(this);
	BoolectorNode *cond_n = toBoolNode(m_node_i.second);

	c->getTrue()->accept(this);
	BoolectorNode *true_n = toBoolNode(m_node_i.second);

	if (c->getFalse()) {
		c->getFalse()->accept(this);
		BoolectorNode *false_n = toBoolNode(m_node_i.second);

		m_node_i.second = boolector_cond(
				m_solver->btor(),
				cond_n,
				true_n,
				false_n);
		m_node_i.first = false;
	} else {
		// Simple 'implies'
		m_node_i.second = boolector_implies(
				m_solver->btor(),
				cond_n,
				true_n);
		m_node_i.first = false;
	}

	DEBUG_LEAVE("visitModelConstrainIfElse");
}

void SolverBoolectorSolveModelBuilder::visitModelConstraintImplies(
		dm::IModelConstraintImplies *c) {
	DEBUG_ENTER("visitModelConstraintImplies");
	c->getCond()->accept(this);
	BoolectorNode *cond_n = toBoolNode(m_node_i.second);

	c->getBody()->accept(this);
	BoolectorNode *body_n = toBoolNode(m_node_i.second);

	m_node_i.second = boolector_implies(
			m_solver->btor(),
			cond_n,
			body_n);
	m_node_i.first = false;
	DEBUG_LEAVE("visitModelConstraintImplies");
}

void SolverBoolectorSolveModelBuilder::visitModelConstraintScope(dm::IModelConstraintScope *c) {
	DEBUG_ENTER("visitModelConstraintScope");
	BoolectorNode *result = 0;

	for (auto c_it=c->constraints().begin();
			c_it!=c->constraints().end(); c_it++) {
		m_node_i = {false, 0};
		(*c_it)->accept(this);

		if (m_node_i.second) {
			// Ensure the term is a boolean
			BoolectorNode *n = toBoolNode(m_node_i.second);

			if (result) {
				// Need to AND together
				result = boolector_and(m_solver->btor(),
						result,
						n);
			} else {
				result = n;
			}
		}
	}

	m_node_i = {false, result};
	DEBUG_LEAVE("visitModelConstraintScope");
}

void SolverBoolectorSolveModelBuilder::visitModelConstraintSoft(dm::IModelConstraintSoft *c) {

}

void SolverBoolectorSolveModelBuilder::visitModelExprBin(dm::IModelExprBin *e) {
	DEBUG_ENTER("visitModelExprBin %s", dm::BinOp2Str_s(e->op()));
	int32_t ctx_width = m_width_s.back();

	if (e->lhs()->width() > ctx_width) {
		ctx_width = e->lhs()->width();
	}
	if (e->rhs()->width() > ctx_width) {
		ctx_width = e->rhs()->width();
	}

	node_info_t lhs_i = expr(e->lhs(), ctx_width);
	node_info_t rhs_i = expr(e->rhs(), ctx_width);

	bool is_signed = (lhs_i.first && rhs_i.first);

	BoolectorNode *lhs_n;
	BoolectorNode *rhs_n;

	if (e->op() == dm::BinOp::LogAnd || e->op() == dm::BinOp::LogOr) {
		lhs_n = toBoolNode(lhs_i.second);
		rhs_n = toBoolNode(rhs_i.second);
	} else {
		lhs_n =	extend(lhs_i.second, ctx_width, is_signed);
		rhs_n =	extend(rhs_i.second, ctx_width, is_signed);
	}

	BoolectorNode *result = 0;

	switch (e->op()) {
	case dm::BinOp::Eq:
		result = boolector_eq(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::Ne:
		result = boolector_ne(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::Gt:
		if (is_signed) {
			result = boolector_sgt(m_solver->btor(), lhs_n, rhs_n);
		} else {
			result = boolector_ugt(m_solver->btor(), lhs_n, rhs_n);
		}
		break;
	case dm::BinOp::Ge:
		if (is_signed) {
			result = boolector_sgte(m_solver->btor(), lhs_n, rhs_n);
		} else {
			result = boolector_ugte(m_solver->btor(), lhs_n, rhs_n);
		}
		break;
	case dm::BinOp::Lt:
		if (is_signed) {
			result = boolector_slt(m_solver->btor(), lhs_n, rhs_n);
		} else {
			result = boolector_ult(m_solver->btor(), lhs_n, rhs_n);
		}
		break;
	case dm::BinOp::Le:
		if (is_signed) {
			result = boolector_slte(m_solver->btor(), lhs_n, rhs_n);
		} else {
			result = boolector_ulte(m_solver->btor(), lhs_n, rhs_n);
		}
		break;
	case dm::BinOp::Add:
		result = boolector_add(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::Sub:
		result = boolector_sub(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::Div:
		result = boolector_udiv(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::Mul:
		result = boolector_mul(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::Mod:
		if (is_signed) {
			result = boolector_srem(m_solver->btor(), lhs_n, rhs_n);
		} else {
			result = boolector_urem(m_solver->btor(), lhs_n, rhs_n);
		}
		break;
	case dm::BinOp::BinAnd:
		result = boolector_and(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::BinOr:
		result = boolector_or(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::Xor:
		result = boolector_xor(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::LogAnd:
		result = boolector_and(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::LogOr:
		result = boolector_or(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::Sll:
		result = boolector_sll(m_solver->btor(), lhs_n, rhs_n);
		break;
	case dm::BinOp::Srl:
		result = boolector_srl(m_solver->btor(), lhs_n, rhs_n);
		break;
	default:
		fprintf(stdout, "Error: unhandle bin op %d\n", e->op());
		break;

	}

	// If the context wants something larger, perform extension on the
	// backend vs the frontend. Not sure this is more efficient, but
	// shouldn't be worse
	if (e->op() == dm::BinOp::LogAnd || e->op() == dm::BinOp::LogOr) {
		if (ctx_width != 1) {
			result = extend(result, ctx_width, false);
		}
	}

	m_node_i = {is_signed, result};

	DEBUG_LEAVE("visitModelExprBin %s", dm::BinOp2Str_s(e->op()));
}

void SolverBoolectorSolveModelBuilder::visitModelExprFieldRef(dm::IModelExprFieldRef *e) {
	// Note: this should only be used for scalar fields
	BoolectorNode *n;
	bool is_signed = false;
	DEBUG_ENTER("visitModelExprFieldRef %s", e->field()->name().c_str());

	// This takes care of finding and/or constructing the field
	visitModelField(e->field());

	DEBUG_LEAVE("visitModelExprFieldRef %s n=%p",
			e->field()->name().c_str(), m_node_i.second);
}

void SolverBoolectorSolveModelBuilder::visitModelExprIn(dm::IModelExprIn *e) {
	DEBUG_ENTER("visitModelExprIn");
	int32_t ctx_width = m_width_s.back();
	node_info_t lhs_i = expr(e->lhs(), ctx_width);

	if (e->lhs()->width() > ctx_width) {
		ctx_width = e->lhs()->width();
	}

	if (e->rangelist()->width() > ctx_width) {
		ctx_width = e->rangelist()->width();
	}

	// TODO: need to consider sign of rangelist elements?
	bool is_signed = lhs_i.first;

	lhs_i.second = extend(lhs_i.second, ctx_width, is_signed);

	BoolectorNode *n = 0;

	for (auto r_it=e->rangelist()->ranges().begin();
			r_it!=e->rangelist()->ranges().end(); r_it++) {
		BoolectorNode *t;
		// Individual term
		if ((*r_it)->upper()) {
			// Dual-value
			node_info_t rvl_i = expr((*r_it)->lower(), ctx_width);
			node_info_t rvu_i = expr((*r_it)->upper(), ctx_width);
			if (is_signed) {
				t = boolector_and(
						m_solver->btor(),
						boolector_sgte(
								m_solver->btor(),
								lhs_i.second,
								rvl_i.second),
						boolector_slte(
								m_solver->btor(),
								lhs_i.second,
								rvu_i.second));
			} else {
				t = boolector_and(
						m_solver->btor(),
						boolector_ugte(
								m_solver->btor(),
								lhs_i.second,
								rvl_i.second),
						boolector_ulte(
								m_solver->btor(),
								lhs_i.second,
								rvu_i.second));
			}
		} else {
			// Single value
			node_info_t rv_i = expr((*r_it)->lower(), ctx_width);
			t = boolector_eq(
					m_solver->btor(),
					lhs_i.second,
					rv_i.second);
		}

		if (!n) {
			n = t;
		} else {
			n = boolector_or(
					m_solver->btor(),
					n,
					t);
		}
	}

	m_node_i.second = n;
	m_node_i.first = false;


	DEBUG_LEAVE("visitModelExprIn");
}

void SolverBoolectorSolveModelBuilder::visitModelExprIndexedFieldRef(
		dm::IModelExprIndexedFieldRef *e) {
	DEBUG_ENTER("visitModelExprIndexedFieldRef");
	dm::IModelField *field = 0;

	for (std::vector<dm::ModelExprIndexedFieldRefElem>::const_iterator
		it=e->getPath().begin();
		it!=e->getPath().end(); it++) {
		switch (it->kind) {
			case dm::ModelExprIndexedFieldRefKind::Field:
				field = it->field;
				break;

			case dm::ModelExprIndexedFieldRefKind::FieldIndex:
				field = field->getField(it->offset);
				break;

			case dm::ModelExprIndexedFieldRefKind::VecIndex:
				fprintf(stdout, "TODO: vector index\n");
				field = 0;
				break;
		}
	}

	visitModelField(field);
	DEBUG_LEAVE("visitModelExprIndexedFieldRef");
}

void SolverBoolectorSolveModelBuilder::visitModelExprPartSelect(
		dm::IModelExprPartSelect *e) {
	int32_t ctx_width = m_width_s.back();
	DEBUG_ENTER("visitModelExprPartSelect");
	node_info_t lhs_i = expr(e->lhs(), ctx_width);

	m_node_i.first = false;
	m_node_i.second = boolector_slice(
			m_solver->btor(),
			lhs_i.second,
			e->upper(),
			e->lower());

	DEBUG_LEAVE("visitModelExprPartSelect");
}

void SolverBoolectorSolveModelBuilder::visitModelExprVal(dm::IModelExprVal *e) {
	DEBUG_ENTER("visitModelExprVal");
	char *bits = (char *)alloca(e->val()->bits()+1);
	e->val()->to_bits(bits);

	DEBUG("bits=%s", bits);
	m_node_i.second = boolector_const(m_solver->btor(), bits);
	m_node_i.first = true; // TODO:

	DEBUG_LEAVE("visitModelExprVal");
}

void SolverBoolectorSolveModelBuilder::visitModelField(dm::IModelField *f) {
	DEBUG_ENTER("visitModelField %s", f->name().c_str());
	if (f->isFlagSet(dm::ModelFieldFlag::UsedRand)) {
		if (m_build_field) {
			// If we're field-building, then the solver already
			// knows it doesn't have a cached copy
			m_node_i.second = 0;
		} else {
			m_node_i.second = m_solver->findFieldData(f);
			m_node_i.first = dm::TaskIsDataTypeSigned().check(f->getDataType());
		}

		if (!m_node_i.second) {
			DEBUG("Creating a new field node %s", f->name().c_str());
			// Hasn't been initialized yet
			m_field_s.push_back(f);
			if (f->getDataType()) {
				f->getDataType()->accept(this);
			} else {
				DEBUG("Note: no datatype");
			}
			m_field_s.pop_back();
			m_solver->addFieldData(f, m_node_i.second);
		}
	} else {
		// Create a value node
		char *bits = (char *)alloca(f->val()->bits()+1);
		f->val()->to_bits(bits);

		DEBUG("bits=%s", bits);
		m_node_i.second = boolector_const(m_solver->btor(), bits);
		m_node_i.first = false; // TODO:
	}
	DEBUG_LEAVE("visitModelField %s", f->name().c_str());
}

void SolverBoolectorSolveModelBuilder::visitModelFieldRoot(dm::IModelFieldRoot *f) {
	DEBUG_ENTER("visitModelFieldRoot %s", f->name().c_str());
	VisitorBase::visitModelFieldRoot(f);
	DEBUG_LEAVE("visitModelFieldRoot %s", f->name().c_str());
}

void SolverBoolectorSolveModelBuilder::visitModelFieldType(dm::IModelFieldType *f) {
	DEBUG_ENTER("visitModelFieldType %s(%p)", f->name().c_str(), f);
	if (f->isFlagSet(dm::ModelFieldFlag::UsedRand)) {
		// Create a solver-variable representation for this
		if (m_build_field) {
			m_node_i.second = 0;
		} else {
			m_node_i.second = m_solver->findFieldData(f);
			m_node_i.first = dm::TaskIsDataTypeSigned().check(f->getDataType());
		}

		if (!m_node_i.second) {
			DEBUG("Creating a new field node %s", f->name().c_str());
			// Hasn't been initialized yet
			m_field_s.push_back(f);
			if (f->getDataType()) {
				f->getDataType()->accept(this);
			} else {
				DEBUG("Note: no datatype");
			}
			m_field_s.pop_back();
			m_solver->addFieldData(f, m_node_i.second);
		}
	} else {
		// Create a value node
		char *bits = (char *)alloca(f->val()->bits()+1);
		f->val()->to_bits(bits);

		DEBUG("bits=%s", bits);
		m_node_i.second = boolector_const(m_solver->btor(), bits);
		m_node_i.first = dm::TaskIsDataTypeSigned().check(f->getDataType());
	}
	DEBUG_LEAVE("visitModelFieldType %s", f->name().c_str());
}

BoolectorNode *SolverBoolectorSolveModelBuilder::toBoolNode(BoolectorNode *n) {
	uint32_t width;

	if ((width=boolector_get_width(m_solver->btor(), n)) != 1) {
		return boolector_ne(
				m_solver->btor(),
				n,
				boolector_zero(m_solver->btor(),
						m_solver->get_sort(width)));
	} else {
		return n;
	}
}

SolverBoolectorSolveModelBuilder::node_info_t SolverBoolectorSolveModelBuilder::expr(
		dm::IModelExpr 		*e,
		int32_t 		ctx_width) {
	m_node_i = {false, 0};
	m_width_s.push_back(ctx_width);
	e->accept(this);
	m_width_s.pop_back();
	return m_node_i;
}

BoolectorNode *SolverBoolectorSolveModelBuilder::extend(
			BoolectorNode		*n,
			int32_t				ctx_width,
			bool				is_signed) {
	int32_t width = boolector_get_width(m_solver->btor(), n);
	DEBUG_ENTER("extend width=%d ctx_width=%d is_signed=%d",
			width, ctx_width, is_signed);

	if (ctx_width > width) {
		if (is_signed) {
			DEBUG("Sign-extend");
			n = boolector_sext(
					m_solver->btor(),
					n,
					(ctx_width-width));
		} else {
			DEBUG("Unsigned-extend");
			n = boolector_uext(
					m_solver->btor(),
					n,
					(ctx_width-width));
		}
	}
	DEBUG_LEAVE("extend width=%d ctx_width=%d is_signed=%d",
			width, ctx_width, is_signed);
	return n;
}

dmgr::IDebug *SolverBoolectorSolveModelBuilder::m_dbg = 0;

}
}
