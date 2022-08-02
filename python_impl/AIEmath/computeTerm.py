# termGenerator.py
# computes a, b, c and writes to file
# this generates a golden output for use in testing later.
import math

def fast_exp(a, prec=16):
    ''' Fast function to compute e^a
        Trades accuracy for speed, result will be inaccurate
    '''
    a = 1.0 + a / 2**prec;
    for i in range(prec):
        a *= a;
    return a;

def a_plus(sorted_list, gamma):
    ''' Compute term a+ = e^{(x_i - x_e^+) / gamma}
        The entire sorted list is needed to take advantage of max x
    '''
    a_plus_results = [0] * len(sorted_list)
    for i in range(len(sorted_list)):
        exp = (sorted_list[i] - sorted_list[-1]) / gamma
        a_plus_results[i] = fast_exp(exp)
    return a_plus_results

def a_minus(sorted_list, gamma):
    ''' Compute term a- = e^{(x_e^- - x_i) / gamma}
        The entire sorted list is needed to take advantage of min x
    '''
    a_minus_results = [0] * len(sorted_list)
    a_minus_results[0] = 1.0 # first term is always e^0 = 1
    for i in range(1, len(sorted_list)):
        exp = (sorted_list[0] - sorted_list[i]) / gamma
        a_minus_results[i] = fast_exp(exp)
    return a_minus_results

def b_term(input_a):
    return sum(input_a)

def c_term(input_x, input_a):
    sum = 0;
    for i in range(len(input_x)):
        sum += input_x[i] * input_a[i]
    return sum

def WA_hpwl(b_plus, c_plus, b_minus, c_minus):
    ''' Compute an estimate for the HPWL (x coordinate only)'''
    return (c_plus / b_plus) - (c_minus / b_minus)


def WA_partial(x_i, ai_plus, b_plus, c_plus, ai_minus, b_minus, c_minus, gamma):
    ''' Compute the partial derivative of the wirelength estimate, 
        with repect to a particular x_i
    '''
    term1 = ((1 + x_i/gamma)*b_plus  - c_plus/gamma )*ai_plus  / (b_plus * b_plus)
    term2 = ((1 - x_i/gamma)*b_minus + c_minus/gamma)*ai_minus / (b_minus * b_minus)
    return term1 - term2

def density(u, v, rho):
    ''' @brief compute a_uv term for density function
        @param u, v as in DREAMplace 0..M
        @param rho: density map MxM
    '''
    M = len(rho)
    w = 2*math.pi / M
    sum = 0
    for x in range(M):
        for y in range(M):
            sum += rho[x][y] * math.cos(w*u*x) * math.cos(w*v*y)
    sum /= M*M
    return sum

def phi(u, v, alpha):
    ''' @brief compute a_uv term for density function
        @param u, v as in DREAMplace 0..M
        @param rho: density map MxM
    '''
    if u == 0 and v == 0:
        return 0
    M = len(alpha)
    w = 2*math.pi / M
    sum = 0
    for x in range(M):
        for y in range(M):
            sum += alpha[x][y]/(w*u*w*u + w*v*w*v) * math.cos(w*u*x) * math.cos(w*v*y)
    return sum

    
    
