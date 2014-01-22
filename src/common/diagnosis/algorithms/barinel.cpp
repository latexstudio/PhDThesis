#include "barinel.h"
#include "utils/iostream.h"

#include <boost/foreach.hpp>

namespace diagnosis {
namespace algorithms {
using namespace structs;

t_barinel_model::t_barinel_model (const t_spectra & spectra,
                                  const t_candidate & candidate,
                                  bool use_fuzzy_error,
                                  bool use_confidence,
                                  const t_spectra_filter * filter) {
    assert(candidate.size() < sizeof(t_component_id) * 8);

    t_spectra_filter tmp_filter;

    if (filter)
        tmp_filter = *filter;

    // Init filter
    tmp_filter.resize_components(spectra.get_component_count());
    tmp_filter.filter_all_components_but(candidate);

    t_spectra_iterator it(spectra.get_component_count(),
                          spectra.get_transaction_count(),
                          &tmp_filter);

    // Set container size
    pass.resize(1 << candidate.size(), 0);
    fail.resize(1 << candidate.size(), 0);

    while (it.next_transaction()) {
        t_id symbol = 0;

        for (t_id comp = 0; it.next_component(); comp++) {
            if (spectra.get_activations(it.get_component(),
                                        it.get_transaction()))
                symbol += 1 << comp;
        }

        if (symbol) {
            t_confidence conf = use_confidence ? spectra.get_confidence(it.get_transaction()) : 1;
            t_error err = use_fuzzy_error ? spectra.get_error(it.get_transaction()) : spectra.is_error(it.get_transaction());
            pass[symbol] += conf * (1 - err);
            fail[symbol] += conf * err;
        }
    }

    // std::cout << "Symbols p:" << pass << " f:" << fail << std::endl;
}

void t_barinel_model::gradient (const t_barinel_goodnesses & goodnesses,
                                t_barinel_goodnesses & ret) const {
    size_t precision = goodnesses[0].get_prec();
    t_goodness_mp tmp_f(0, precision);


    ret = t_barinel_goodnesses(goodnesses.size(), t_goodness_mp(0, precision));

    for (t_id pattern = 1; pattern < (1 << (goodnesses.size())); pattern++) {
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

void t_barinel_model::update (const t_barinel_goodnesses & g,
                              const t_barinel_goodnesses & grad,
                              t_barinel_goodnesses & ret,
                              double lambda) const {
    t_goodness_mp lambda_mp(lambda, g[0].get_prec());


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

t_barinel::t_barinel () {
    g_j = 0.001;
    epsilon = 0.0001;
    lambda = 1e-20;
    precision = 128;
    iterations = 0;
    use_fuzzy_error = true;
    use_confidence = true;
}

t_barinel::t_barinel (size_t precision) {
    g_j = 0.001;
    epsilon = 0.0001;
    lambda = 1e-20;
    iterations = 0;
    use_fuzzy_error = true;
    use_confidence = true;
    this->precision = precision;
}

void t_barinel::operator () (const structs::t_spectra & spectra,
                             const structs::t_trie & D,
                             t_ret_type & probs,
                             const structs::t_spectra_filter * filter) const {
    t_trie::iterator it = D.begin();


    while (it != D.end()) {
        t_probability_mp ret;
        (* this)(spectra, * it, ret);
        probs.push_back(ret);
        it++;
    }
}

void t_barinel::operator () (const t_spectra & spectra,
                             const t_candidate & candidate,
                             t_probability_mp & ret,
                             const t_spectra_filter * filter) const {
    // std::cerr << "Barinel candidate " << candidate << std::endl;
    t_barinel_model m(spectra,
                      candidate,
                      use_fuzzy_error,
                      use_confidence,
                      filter);


    t_barinel_goodnesses g(candidate.size(), t_goodness_mp(0.5, precision));
    t_barinel_goodnesses g_old(candidate.size(), t_goodness_mp(0.5, precision));

    t_probability_mp pr(0, precision);
    t_probability_mp pr_old(0, precision);

    t_barinel_goodnesses grad(candidate.size(), t_goodness_mp(0.5, precision));

    t_count it = 0;

    double lambda = 1;


    // Calculate probability
    probability(spectra, candidate, g, pr, filter);

    while (it++ < iterations || !iterations) {
        // Update olds
        pr_old = pr;
        g_old = g;

        // Update goodness values;
        m.gradient(g_old, grad);

        m.update(g_old, grad, g, lambda);

        // Calculate probability
        probability(spectra, candidate, g, pr, filter);

        // Overshooting
        if (pr <= pr_old) {
            while (pr <= pr_old && lambda >= this->lambda) {
                lambda /= 8;
                m.update(g_old, grad, g, lambda);

                // std::cerr << "it: " << it << " Overshooting... Pr_old: " << pr_old << " Pr: " << pr << " g_old: " << g_old << " g: " << g << " updating lambda = " << lambda << std::endl;
                // Calculate probability
                probability(spectra, candidate, g, pr, filter);
            }
        }
        // Look for undershooting
        else {
            t_probability_mp pr_tmp(0, precision);
            t_barinel_goodnesses g_tmp(candidate.size(), t_goodness_mp(0.5, precision));

            while (true) {
                m.update(g_old, grad, g_tmp, lambda * 2);
                probability(spectra, candidate, g_tmp, pr_tmp, filter);

                if (pr < pr_tmp) {
                    pr = pr_tmp;
                    g = g_tmp;
                    lambda *= 2;
                    // std::cerr << "it: " << it << " Undershooting... Pr: " << pr << " Pr_tmp: " << pr_tmp << " updating lambda = " << lambda << std::endl;
                }
                else break;
            }
        }

        // Check stop conditions
        if (lambda < this->lambda)
            break;

        // if ((pr - pr_old) < epsilon)
        if (2 * (pr - pr_old) / abs(pr + pr_old) < epsilon)
            break;

        // std::cerr << "Old Probability: " << pr_old << " Probability: " << pr << std::endl;
    }

    t_probability_mp prior_pr;
    prior(candidate, prior_pr);

    ret = prior_pr * pr;
}

void t_barinel::prior (const t_candidate & candidate,
                       t_goodness_mp & ret) const {
    ret = pow(g_j, candidate.size());
}
}
}