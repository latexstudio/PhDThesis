#include "fuzzinel.h"
#include "../utils/iostream.h"

#include <boost/foreach.hpp>
#include <list>

namespace diagnostic {
namespace algorithms {

typedef std::vector<t_goodness_mp> t_fuzzinel_goodnesses;

void probability (const t_spectrum & spectrum,
                  const t_candidate & candidate,
                  const t_fuzzinel_goodnesses & goodnesses,
                  t_probability_mp & ret,
                  const t_spectrum_filter * filter=NULL,
                  bool use_confidence=true,
                  bool use_fuzzy_error=true,
                  bool use_count=false) {
    assert(candidate.size() > 0);

    t_goodness_mp tmp(goodnesses[0]);

    ret = 1;

    t_spectrum_iterator it(spectrum.get_component_count(),
                          spectrum.get_transaction_count(),
                          filter);

    while (it.transaction.next()) {
        tmp = 1;
        t_candidate::const_iterator c_it = candidate.begin();
        t_id comp = 0;

        while (c_it != candidate.end()) {
            t_count count = spectrum.get_activations(*c_it, it.transaction.get());

            assert(goodnesses[comp] >= 0);
            assert(goodnesses[comp] <= 1);

            if (count) {
                if (use_count)
                    tmp *= pow(goodnesses[comp], count);
                else
                    tmp *= goodnesses[comp];
            }

            c_it++;
            comp++;
        }

        // Fuzzy health
        t_error e = use_fuzzy_error ? spectrum.get_error(it.transaction.get()) : spectrum.is_error(it.transaction.get());


        tmp = e * (1 - tmp) + (1 - e) * tmp;

        // Confidence scaling
        if (use_confidence) {
            t_confidence c = spectrum.get_confidence(it.transaction.get());
            tmp = (1 - c) + (c * tmp);
        }

        ret *= tmp;
    }

    assert(ret >= 0);
    assert(ret <= 1);
}

void prior (const t_candidate & candidate,
            t_goodness_mp & ret) {
    ret = pow(0.001, candidate.size());
}

class t_fuzzinel_model {
public:
    t_fuzzinel_model (const t_spectrum & spectrum,
                     const t_candidate & candidate,
                     bool use_fuzzy_error=true,
                     bool use_confidence=true,
                     const t_spectrum_filter * filter=NULL);

    virtual void gradient (const t_fuzzinel_goodnesses & goodnesses,
                           t_fuzzinel_goodnesses & ret) const;

    virtual void update (const t_fuzzinel_goodnesses & g,
                         const t_fuzzinel_goodnesses & grad,
                         t_fuzzinel_goodnesses & ret,
                         double lambda) const;

private:
    std::vector<t_error> pass;
    std::vector<t_error> fail;
};



t_fuzzinel_model::t_fuzzinel_model (const t_spectrum & spectrum,
                                  const t_candidate & candidate,
                                  bool use_fuzzy_error,
                                  bool use_confidence,
                                  const t_spectrum_filter * filter) {
    assert(candidate.size() < sizeof(t_component_id) * 8);

    t_spectrum_filter tmp_filter;

    if (filter)
        tmp_filter = *filter;

    // Init filter
    tmp_filter.components.resize(spectrum.get_component_count());
    tmp_filter.components.filter_all_but(candidate);

    t_spectrum_iterator it(spectrum.get_component_count(),
                          spectrum.get_transaction_count(),
                          &tmp_filter);

    // Set container size
    pass.resize(1 << candidate.size(), 0);
    fail.resize(1 << candidate.size(), 0);

    while (it.transaction.next()) {
        t_id symbol = 0;

        for (t_id comp = 0; it.component.next(); comp++) {
            if (spectrum.get_activations(it.component.get(),
                                        it.transaction.get()))
                symbol += 1 << comp;
        }

        if (symbol) {
            t_confidence conf = use_confidence ? spectrum.get_confidence(it.transaction.get()) : 1;
            t_error err = use_fuzzy_error ? spectrum.get_error(it.transaction.get()) : spectrum.is_error(it.transaction.get());
            pass[symbol] += conf * (1 - err);
            fail[symbol] += conf * err;
        }
    }
}

void t_fuzzinel_model::gradient (const t_fuzzinel_goodnesses & goodnesses,
                                t_fuzzinel_goodnesses & ret) const {
    t_goodness_mp tmp_f(0);


    ret = t_fuzzinel_goodnesses(goodnesses.size(), t_goodness_mp(0));

    for (t_id pattern = 1; pattern < (1u << (goodnesses.size())); pattern++) {
        // PASS stuff
        for (t_id c = 0; c < goodnesses.size(); c++)
            ret[c] += pass[pattern] / goodnesses[c];

        // FAIL stuff
        tmp_f = 1;

        t_id pattern_tmp = pattern;

        for (t_id c = 0; pattern_tmp; c++, pattern_tmp >>= 1)
            if (pattern_tmp & 1)
                tmp_f *= goodnesses[c];

        pattern_tmp = pattern;

        for (t_id c = 0; pattern_tmp; c++, pattern_tmp >>= 1)
            if (pattern_tmp & 1)
                ret[c] += -fail[pattern] * (tmp_f / goodnesses[c]) / (1 - tmp_f);
    }
}

void t_fuzzinel_model::update (const t_fuzzinel_goodnesses & g,
                              const t_fuzzinel_goodnesses & grad,
                              t_fuzzinel_goodnesses & ret,
                              double lambda) const {
    t_goodness_mp lambda_mp(lambda);


    for (t_id c = 0; c < g.size(); c++) {
        if (grad[c] == 0)
            continue;

        ret[c] = g[c] + (lambda_mp * g[c] / grad[c]);

        if (ret[c] <= 0)
            ret[c] = 0;
        else if (ret[c] >= 1)
            ret[c] = 1;
    }
}

t_fuzzinel::t_fuzzinel (size_t precision) {
    epsilon = 0.0001;
    lambda = 1;
    iterations = 100;
    use_fuzzy_error = true;
    use_confidence = true;
}

void t_fuzzinel::operator () (const t_spectrum & spectrum,
                             const t_trie & D,
                             t_ret_type & probs,
                             const t_spectrum_filter * filter) const {
    std::list<t_probability_mp> probs_mp;
    t_probability_mp total = 0;

    BOOST_FOREACH(auto & candidate, D) {
        t_probability_mp ret;
        (* this)(spectrum, candidate, ret);
        total += ret;
        probs_mp.push_back((t_ret_type::value_type) ret);
    }

    BOOST_FOREACH(auto & prob, probs_mp) {
        t_ret_type::value_type ret = (prob / total).convert_to<t_ret_type::value_type>();
        probs.push_back(ret);
    }
}

void t_fuzzinel::operator () (const t_spectrum & spectrum,
                             const t_candidate & candidate,
                             t_probability_mp & ret,
                             const t_spectrum_filter * filter) const {
    t_fuzzinel_model m(spectrum,
                      candidate,
                      use_fuzzy_error,
                      use_confidence,
                      filter);


    t_fuzzinel_goodnesses g(candidate.size(), t_goodness_mp(0.5));
    t_fuzzinel_goodnesses g_old(candidate.size(), t_goodness_mp(0.5));

    t_probability_mp pr(0);
    t_probability_mp pr_old(0);

    t_fuzzinel_goodnesses grad(candidate.size(), t_goodness_mp(0.5));

    t_count it = 0;


    // Calculate probability
    probability(spectrum, candidate, g, pr, filter);

    while (it++ < iterations || !iterations) {
        // Update olds
        pr_old = pr;
        g_old = g;

        // Update goodness values;
        m.gradient(g_old, grad);

        m.update(g_old, grad, g, lambda);

        // Calculate probability
        probability(spectrum, candidate, g, pr, filter);

        // Check stop condition
        if (2 * (pr - pr_old) / abs(pr + pr_old) < epsilon)
            break;
    }

    t_probability_mp prior_pr;
    prior(candidate, prior_pr);

    ret = prior_pr * pr;
}


void t_fuzzinel::json_configs (t_configs & out) const {
    out["use_fuzzy_error"] = std::to_string(use_fuzzy_error);
    out["use_confidence"] = std::to_string(use_confidence);
}


}
}
